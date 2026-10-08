#include "config_test_helpers.h"

#include "unity.h"

#include <cstring>
#include <string>

using namespace OpenShock;

namespace ConfigTest {
  Config::RootConfig MakeSampleConfig()
  {
    Config::RootConfig config;

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
    config.lan           = Config::LanConfig(true, "lan_key_0123456789");

    return config;
  }

  std::vector<uint8_t> Serialize(const Config::RootConfig& config, bool withSensitiveData)
  {
    flatbuffers::FlatBufferBuilder builder;
    auto root = config.ToFlatbuffers(builder, withSensitiveData);
    Serialization::Configuration::FinishHubConfigBuffer(builder, root);
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
  }

  bool Deserialize(const uint8_t* data, std::size_t size, Config::RootConfig& out)
  {
    if (size > Config::MaxConfigSize) {
      return false;  // the Verifier constructor would assert
    }
    flatbuffers::Verifier::Options options {
      .max_size = Config::MaxConfigSize + 1,
    };
    flatbuffers::Verifier verifier(data, size, options);
    if (!verifier.VerifyBuffer<Serialization::Configuration::HubConfig>()) {
      return false;
    }
    return out.FromFlatbuffers(flatbuffers::GetRoot<Serialization::Configuration::HubConfig>(data));
  }

  bool InitFrom(const uint8_t* data, std::size_t size)
  {
    HostConfigFs::reset();
    if (data != nullptr) {
      HostConfigFs::files()[kConfigPath].assign(data, data + size);
    }
    Config::Init();
    return HostConfigFs::writeCount == 0;
  }

  void InitDefault()
  {
    InitFrom(nullptr, 0);
    HostConfigFs::writeCount = 0;
  }

  std::vector<uint8_t> StoredFile()
  {
    auto it = HostConfigFs::files().find(kConfigPath);
    return it == HostConfigFs::files().end() ? std::vector<uint8_t>() : it->second;
  }

  Config::RootConfig Snapshot()
  {
    Config::RootConfig config;
    TEST_ASSERT_TRUE(Config::GetRFConfig(config.rf));
    TEST_ASSERT_TRUE(Config::GetWiFiConfig(config.wifi));
    TEST_ASSERT_TRUE(Config::GetCaptivePortalConfig(config.captivePortal));
    TEST_ASSERT_TRUE(Config::GetBackendConfig(config.backend));
    TEST_ASSERT_TRUE(Config::GetSerialInputConfig(config.serialInput));
    TEST_ASSERT_TRUE(Config::GetOtaUpdateConfig(config.otaUpdate));
    TEST_ASSERT_TRUE(Config::GetEStopConfig(config.estop));
    TEST_ASSERT_TRUE(Config::GetLanConfig(config.lan));
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

  void AssertConfigEqual(const Config::RootConfig& expected, const Config::RootConfig& actual, bool withSensitiveData)
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
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(eo.updateChannel), static_cast<uint8_t>(ao.updateChannel), "otaUpdate.updateChannel");
    TEST_ASSERT_EQUAL_MESSAGE(eo.checkOnStartup, ao.checkOnStartup, "otaUpdate.checkOnStartup");
    TEST_ASSERT_EQUAL_MESSAGE(eo.checkPeriodically, ao.checkPeriodically, "otaUpdate.checkPeriodically");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(eo.checkInterval, ao.checkInterval, "otaUpdate.checkInterval");
    TEST_ASSERT_EQUAL_MESSAGE(eo.allowBackendManagement, ao.allowBackendManagement, "otaUpdate.allowBackendManagement");
    TEST_ASSERT_EQUAL_MESSAGE(eo.requireManualApproval, ao.requireManualApproval, "otaUpdate.requireManualApproval");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(eo.updateId, ao.updateId, "otaUpdate.updateId");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(eo.updateStep), static_cast<uint8_t>(ao.updateStep), "otaUpdate.updateStep");

    TEST_ASSERT_EQUAL_MESSAGE(expected.estop.enabled, actual.estop.enabled, "estop.enabled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected.estop.gpioPin, actual.estop.gpioPin, "estop.gpioPin");

    TEST_ASSERT_EQUAL_MESSAGE(expected.lan.apiKeyEnabled, actual.lan.apiKeyEnabled, "lan.apiKeyEnabled");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(withSensitiveData ? expected.lan.apiKey.c_str() : "", actual.lan.apiKey.c_str(), "lan.apiKey");
  }
}  // namespace ConfigTest
