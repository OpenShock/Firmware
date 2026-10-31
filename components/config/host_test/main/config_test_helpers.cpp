#include <freertos/FreeRTOS.h>

#include "config_test_helpers.h"

#include "unity.h"

#include "json/Json.h"

#include <nvs_flash.h>

#include <cstring>
#include <string>
#include <utility>

using namespace OpenShock;

namespace ConfigTest {
  bool Full::SaveNvs(const char* partition) const
  {
    if (!rf.SaveNvs(partition, nullptr) || !wifi.SaveNvs(partition, nullptr) || !captivePortal.SaveNvs(partition, nullptr) || !backend.SaveNvs(partition, nullptr) || !serialInput.SaveNvs(partition, nullptr) || !otaUpdate.SaveNvs(partition, nullptr)
        || !estop.SaveNvs(partition, nullptr))
    {
      return false;
    }
    auto root = Config::Nvs::Handle::Open(partition, Keys::Ns::kRoot, true);
    return root && root->SetU8(Keys::Root::kSchema, Keys::kSchemaVersion) && root->Commit();
  }

  std::string Full::ToJSON(bool withSensitiveData) const
  {
    JSON::StringWriter writer;
    json_gen_str_t* gen = writer.gen();
    json_gen_start_object(gen);
    rf.ToJSON(gen, "rf", withSensitiveData);
    wifi.ToJSON(gen, "wifi", withSensitiveData);
    captivePortal.ToJSON(gen, "captivePortal", withSensitiveData);
    backend.ToJSON(gen, "backend", withSensitiveData);
    serialInput.ToJSON(gen, "serialInput", withSensitiveData);
    otaUpdate.ToJSON(gen, "otaUpdate", withSensitiveData);
    estop.ToJSON(gen, "estop", withSensitiveData);
    json_gen_end_object(gen);
    return writer.finish();
  }

  Full MakeSampleConfig()
  {
    Full config;

    config.rf = Config::RFConfig(GPIO_NUM_25, false);

    Config::WiFiCredentials home(1, "HomeNet", "hunter22", WIFI_AUTH_WPA2_PSK);
    home.bssid = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    Config::WiFiCredentials guest(2, "Guest", "", WIFI_AUTH_OPEN);
    Config::WiFiCredentials office(7, "Office \"5G\"", "p@ss w0rd/\\", WIFI_AUTH_WPA3_PSK);
    config.wifi = Config::WiFiConfig("MyHubAP", "my-hub", {home, guest, office});

    config.captivePortal = Config::CaptivePortalConfig(true);
    config.backend       = Config::BackendConfig("api.example.org", "tok_0123456789abcdef");
    config.serialInput   = Config::SerialInputConfig(false);
    config.otaUpdate     = Config::OtaUpdateConfig(false, "fw.example.org", OtaUpdateChannel::Develop, true, true, 120, false, true, 4242, OtaUpdateStep::Validating);
    config.estop         = Config::EStopConfig(true, GPIO_NUM_13);

    return config;
  }

  std::vector<uint8_t> Serialize(const Full& config, bool withSensitiveData)
  {
    // ForceDefaults like the firmware's own writes, so the bytes match what Config stores and GetRaw returns
    flatbuffers::FlatBufferBuilder builder;
    builder.ForceDefaults(true);
    auto rf            = config.rf.ToFlatbuffers(builder, withSensitiveData);
    auto wifi          = config.wifi.ToFlatbuffers(builder, withSensitiveData);
    auto captivePortal = config.captivePortal.ToFlatbuffers(builder, withSensitiveData);
    auto backend       = config.backend.ToFlatbuffers(builder, withSensitiveData);
    auto serialInput   = config.serialInput.ToFlatbuffers(builder, withSensitiveData);
    auto otaUpdate     = config.otaUpdate.ToFlatbuffers(builder, withSensitiveData);
    auto estop         = config.estop.ToFlatbuffers(builder, withSensitiveData);
    auto root          = Serialization::Configuration::CreateHubConfig(builder, rf, wifi, captivePortal, backend, serialInput, otaUpdate, estop);
    Serialization::Configuration::FinishHubConfigBuffer(builder, root);
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
  }

  bool Deserialize(const uint8_t* data, std::size_t size, Full& out)
  {
    auto json = Config::HubConfigToJSON(std::span<const uint8_t>(data, size));
    if (!json) {
      return false;
    }
    JSON::JsonDocument doc;
    if (!doc.parse(*json)) {
      return false;
    }
    JSON::JsonView root = doc.root();
    return out.rf.FromJSON(root["rf"]) && out.wifi.FromJSON(root["wifi"]) && out.captivePortal.FromJSON(root["captivePortal"]) && out.backend.FromJSON(root["backend"]) && out.serialInput.FromJSON(root["serialInput"])
        && out.otaUpdate.FromJSON(root["otaUpdate"]) && out.estop.FromJSON(root["estop"]);
  }

  static void wipePartition(const char* label)
  {
    nvs_flash_deinit_partition(label);  // ESP_ERR_NVS_NOT_INITIALIZED when it is not up, which is fine
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_erase_partition(label));
  }

  void WipeStore()
  {
    wipePartition(Keys::kPartitionCfg);
    wipePartition(Keys::kPartitionShared);
    // Back up right away, so a test can write straight into either store; Config::Init's own init is then a no-op
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init_partition(Keys::kPartitionCfg));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_flash_init_partition(Keys::kPartitionShared));
    HostLfs::reset();
  }

  void InitDefault()
  {
    WipeStore();
    Config::Init();
  }

  bool InitFromLegacy(const uint8_t* data, std::size_t size)
  {
    WipeStore();
    Full expected;
    const bool parses = data != nullptr && Deserialize(data, size, expected);
    if (data != nullptr) {
      HostLfs::files()[kLegacyPath].assign(data, data + size);
    }
    Config::Init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, HostLfs::writeCount, "the 1.x file must be left untouched");
    if (!parses) {
      return false;
    }
    // Imported when what the hub runs with is what the file held (as this firmware decodes it)
    return expected.ToJSON() == Snapshot().ToJSON();
  }

  void Reload()
  {
    Config::Init();
  }

  void InitWith(const Full& config)
  {
    WipeStore();
    TEST_ASSERT_TRUE(config.SaveNvs(Keys::kPartitionCfg));
    Reload();
  }

  std::size_t UsedEntries()
  {
    nvs_stats_t stats {};
    TEST_ASSERT_EQUAL(ESP_OK, nvs_get_stats(Keys::kPartitionCfg, &stats));
    return stats.used_entries;
  }

  bool NamespaceExists(const char* partition, const char* ns)
  {
    return OpenShock::Config::Nvs::Handle::Open(partition, ns, false).has_value();
  }

  std::size_t KeyCount(const char* partition, const char* ns)
  {
    auto handle = OpenShock::Config::Nvs::Handle::Open(partition, ns, false);
    if (!handle) {
      return 0;
    }
    std::size_t count = 0;
    TEST_ASSERT_TRUE(handle->ForEachKey(NVS_TYPE_ANY, [&](const char*) { ++count; }));
    return count;
  }

  Full Snapshot()
  {
    Full config;
    config.rf            = Config::GetRFConfig();
    config.wifi          = Config::GetWiFiConfig();
    config.captivePortal = Config::GetCaptivePortalConfig();
    config.backend       = Config::GetBackendConfig();
    config.serialInput   = Config::GetSerialInputConfig();
    config.otaUpdate     = Config::GetOtaUpdateConfig();
    config.estop         = Config::GetEStopConfig();
    return config;
  }

  void AssertCredentialsEqual(const Config::WiFiCredentials& expected, const Config::WiFiCredentials& actual, bool withSensitiveData)
  {
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected.id, actual.id, "wifi credentials id");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.ssid.c_str(), actual.ssid.c_str(), "wifi credentials ssid");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(withSensitiveData ? expected.password.c_str() : "", actual.password.c_str(), "wifi credentials password");
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected.authMode, actual.authMode, "wifi credentials authMode");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.bssid.data(), actual.bssid.data(), 6, "wifi credentials bssid");
  }

  void AssertConfigEqual(const Full& expected, const Full& actual, bool withSensitiveData)
  {
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected.rf.txPin, actual.rf.txPin, "rf.txPin");
    TEST_ASSERT_EQUAL_MESSAGE(expected.rf.keepAliveEnabled, actual.rf.keepAliveEnabled, "rf.keepAliveEnabled");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.wifi.accessPointSSID.c_str(), actual.wifi.accessPointSSID.c_str(), "wifi.accessPointSSID");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.wifi.hostname.c_str(), actual.wifi.hostname.c_str(), "wifi.hostname");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.wifi.credentialsList.size(), actual.wifi.credentialsList.size(), "wifi.credentialsList size");
    for (std::size_t i = 0; i < expected.wifi.credentialsList.size() && i < actual.wifi.credentialsList.size(); ++i) {
      AssertCredentialsEqual(expected.wifi.credentialsList[i], actual.wifi.credentialsList[i], withSensitiveData);
    }

    TEST_ASSERT_EQUAL_MESSAGE(expected.captivePortal.alwaysEnabled, actual.captivePortal.alwaysEnabled, "captivePortal.alwaysEnabled");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected.backend.domain.c_str(), actual.backend.domain.c_str(), "backend.domain");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(withSensitiveData ? expected.backend.authToken.c_str() : "", actual.backend.authToken.c_str(), "backend.authToken");

    TEST_ASSERT_EQUAL_MESSAGE(expected.serialInput.echoEnabled, actual.serialInput.echoEnabled, "serialInput.echoEnabled");

    const auto& eo = expected.otaUpdate;
    const auto& ao = actual.otaUpdate;
    TEST_ASSERT_EQUAL_MESSAGE(eo.isEnabled, ao.isEnabled, "otaUpdate.isEnabled");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(eo.cdnDomain.c_str(), ao.cdnDomain.c_str(), "otaUpdate.cdnDomain");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(std::to_underlying(eo.updateChannel), std::to_underlying(ao.updateChannel), "otaUpdate.updateChannel");
    TEST_ASSERT_EQUAL_MESSAGE(eo.checkOnStartup, ao.checkOnStartup, "otaUpdate.checkOnStartup");
    TEST_ASSERT_EQUAL_MESSAGE(eo.checkPeriodically, ao.checkPeriodically, "otaUpdate.checkPeriodically");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(eo.checkInterval, ao.checkInterval, "otaUpdate.checkInterval");
    TEST_ASSERT_EQUAL_MESSAGE(eo.allowBackendManagement, ao.allowBackendManagement, "otaUpdate.allowBackendManagement");
    TEST_ASSERT_EQUAL_MESSAGE(eo.requireManualApproval, ao.requireManualApproval, "otaUpdate.requireManualApproval");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(eo.updateId, ao.updateId, "otaUpdate.updateId");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(std::to_underlying(eo.updateStep), std::to_underlying(ao.updateStep), "otaUpdate.updateStep");

    TEST_ASSERT_EQUAL_MESSAGE(expected.estop.enabled, actual.estop.enabled, "estop.enabled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected.estop.gpioPin, actual.estop.gpioPin, "estop.gpioPin");
  }
}  // namespace ConfigTest
