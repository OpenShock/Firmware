// Defaults: no stored config, an empty one, or one missing sections/fields must give
// usable defaults rather than a failed load (which would wipe the whole file).
#include "unity.h"

#include "config_test_helpers.h"

#include <string>

using namespace OpenShock;
using namespace ConfigTest;
namespace Fbs = OpenShock::Serialization::Configuration;

namespace {
  std::vector<uint8_t> Finish(flatbuffers::FlatBufferBuilder& builder, flatbuffers::Offset<Fbs::HubConfig> root)
  {
    Fbs::FinishHubConfigBuffer(builder, root);
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
  }
}  // namespace

TEST_CASE("Default values come from Kconfig and the board header", "[config][defaults]")
{
  const Config::RootConfig config;

  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, config.rf.txPin);
  TEST_ASSERT_TRUE(config.rf.keepAliveEnabled);
  TEST_ASSERT_EQUAL_STRING(CONFIG_OPENSHOCK_FW_AP_PREFIX, config.wifi.accessPointSSID.c_str());
  TEST_ASSERT_EQUAL_STRING(CONFIG_OPENSHOCK_FW_HOSTNAME, config.wifi.hostname.c_str());
  TEST_ASSERT_TRUE(config.wifi.credentialsList.empty());
  TEST_ASSERT_FALSE(config.captivePortal.alwaysEnabled);
  TEST_ASSERT_EQUAL_STRING(CONFIG_OPENSHOCK_API_DOMAIN, config.backend.domain.c_str());
  TEST_ASSERT_TRUE(config.backend.authToken.empty());
  TEST_ASSERT_TRUE(config.serialInput.echoEnabled);
  TEST_ASSERT_TRUE(config.otaUpdate.isEnabled);
  TEST_ASSERT_EQUAL_STRING(CONFIG_OPENSHOCK_FW_CDN_DOMAIN, config.otaUpdate.cdnDomain.c_str());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaUpdateChannel::Stable), static_cast<uint8_t>(config.otaUpdate.updateChannel));
  TEST_ASSERT_EQUAL_UINT16(30, config.otaUpdate.checkInterval);
  TEST_ASSERT_TRUE(config.otaUpdate.allowBackendManagement);
  TEST_ASSERT_EQUAL_INT32(0, config.otaUpdate.updateId);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaUpdateStep::None), static_cast<uint8_t>(config.otaUpdate.updateStep));
  // The host-test board has no E-Stop pin, so E-Stop defaults to off.
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_ESTOP_PIN, config.estop.gpioPin);
  TEST_ASSERT_FALSE(config.estop.enabled);
}

TEST_CASE("ToDefault resets every section to the constructed defaults", "[config][defaults]")
{
  Config::RootConfig config = MakeSampleConfig();
  config.ToDefault();
  AssertConfigEqual(Config::RootConfig(), config);
}

TEST_CASE("Missing config file: Init writes a default config that reloads", "[config][defaults]")
{
  TEST_ASSERT_FALSE(InitFrom(nullptr, 0));  // falls back to defaults and writes them
  TEST_ASSERT_EQUAL_INT(1, HostConfigFs::writeCount);
  AssertConfigEqual(Config::RootConfig(), Snapshot());

  const std::vector<uint8_t> written = StoredFile();
  TEST_ASSERT_FALSE(written.empty());
  TEST_ASSERT_TRUE(InitFrom(written));
  AssertConfigEqual(Config::RootConfig(), Snapshot());
}

TEST_CASE("HubConfig with no sections loads as defaults", "[config][defaults]")
{
  flatbuffers::FlatBufferBuilder builder;
  const std::vector<uint8_t> bytes = Finish(builder, Fbs::CreateHubConfig(builder));

  TEST_ASSERT_TRUE(InitFrom(bytes));
  AssertConfigEqual(Config::RootConfig(), Snapshot());
}

TEST_CASE("Missing sections default while present ones are kept", "[config][defaults]")
{
  flatbuffers::FlatBufferBuilder builder;
  auto backend                     = Fbs::CreateBackendConfigDirect(builder, "api.example.org", "tok");
  auto estop                       = Fbs::CreateEStopConfig(builder, true, 13);
  const std::vector<uint8_t> bytes = Finish(builder, Fbs::CreateHubConfig(builder, 0, 0, 0, backend, 0, 0, estop));

  TEST_ASSERT_TRUE(InitFrom(bytes));

  Config::RootConfig expected;
  expected.backend = Config::BackendConfig("api.example.org", "tok");
  expected.estop   = Config::EStopConfig(true, GPIO_NUM_13);
  AssertConfigEqual(expected, Snapshot());
}

TEST_CASE("Missing fields inside a section take their defaults", "[config][defaults]")
{
  flatbuffers::FlatBufferBuilder builder;

  Fbs::RFConfigBuilder rf(builder);
  rf.add_tx_pin(13);
  auto rfOffset = rf.Finish();  // keepalive_enabled absent

  auto hostname = builder.CreateString("my-hub");
  Fbs::WiFiConfigBuilder wifi(builder);
  wifi.add_hostname(hostname);
  auto wifiOffset = wifi.Finish();  // no AP SSID, no credentials

  auto domain = builder.CreateString("api.example.org");
  Fbs::BackendConfigBuilder backend(builder);
  backend.add_domain(domain);
  auto backendOffset = backend.Finish();  // no auth token

  Fbs::OtaUpdateConfigBuilder ota(builder);
  ota.add_update_id(77);
  auto otaOffset = ota.Finish();  // no CDN domain, rest at schema defaults

  const std::vector<uint8_t> bytes = Finish(builder, Fbs::CreateHubConfig(builder, rfOffset, wifiOffset, 0, backendOffset, 0, otaOffset, 0));
  TEST_ASSERT_TRUE(InitFrom(bytes));
  const Config::RootConfig loaded = Snapshot();

  TEST_ASSERT_EQUAL_INT(GPIO_NUM_13, loaded.rf.txPin);
  TEST_ASSERT_TRUE(loaded.rf.keepAliveEnabled);
  TEST_ASSERT_EQUAL_STRING(CONFIG_OPENSHOCK_FW_AP_PREFIX, loaded.wifi.accessPointSSID.c_str());
  TEST_ASSERT_EQUAL_STRING("my-hub", loaded.wifi.hostname.c_str());
  TEST_ASSERT_TRUE(loaded.wifi.credentialsList.empty());
  TEST_ASSERT_EQUAL_STRING("api.example.org", loaded.backend.domain.c_str());
  TEST_ASSERT_TRUE(loaded.backend.authToken.empty());
  TEST_ASSERT_TRUE(loaded.otaUpdate.isEnabled);
  TEST_ASSERT_EQUAL_STRING(CONFIG_OPENSHOCK_FW_CDN_DOMAIN, loaded.otaUpdate.cdnDomain.c_str());
  TEST_ASSERT_TRUE(loaded.otaUpdate.allowBackendManagement);
  TEST_ASSERT_EQUAL_INT32(77, loaded.otaUpdate.updateId);
}

TEST_CASE("Credentials without an SSID are dropped, the rest kept", "[config][defaults]")
{
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<Fbs::WiFiCredentials>> creds {
    Fbs::CreateWiFiCredentialsDirect(builder, 1, "HomeNet", "hunter22"),
    Fbs::CreateWiFiCredentialsDirect(builder, 2, nullptr, "orphan"),
    Fbs::CreateWiFiCredentialsDirect(builder, 3, "", "empty"),
    Fbs::CreateWiFiCredentialsDirect(builder, 4, "Office", nullptr),
  };
  auto wifi = Fbs::CreateWiFiConfig(builder, 0, 0, builder.CreateVector(creds));
  TEST_ASSERT_TRUE(InitFrom(Finish(builder, Fbs::CreateHubConfig(builder, 0, wifi))));

  std::vector<Config::WiFiCredentials> loaded;
  TEST_ASSERT_TRUE(Config::GetWiFiCredentials(loaded));
  TEST_ASSERT_EQUAL_size_t(2, loaded.size());
  AssertCredentialsEqual(Config::WiFiCredentials(1, "HomeNet", "hunter22"), loaded[0]);
  AssertCredentialsEqual(Config::WiFiCredentials(4, "Office", ""), loaded[1]);
}

TEST_CASE("Invalid stored GPIO pins fall back safely", "[config][defaults]")
{
  flatbuffers::FlatBufferBuilder builder;
  auto rf    = Fbs::CreateRFConfig(builder, 24, true);    // GPIO24 does not exist on ESP32
  auto estop = Fbs::CreateEStopConfig(builder, true, 0);  // strapping pin: not a valid E-Stop input
  TEST_ASSERT_TRUE(InitFrom(Finish(builder, Fbs::CreateHubConfig(builder, rf, 0, 0, 0, 0, 0, estop))));

  gpio_num_t txPin;
  TEST_ASSERT_TRUE(Config::GetRFConfigTxPin(txPin));
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, txPin);

  bool estopEnabled = true;
  TEST_ASSERT_TRUE(Config::GetEStopEnabled(estopEnabled));
  TEST_ASSERT_FALSE(estopEnabled);

  // Negative pin (the old "invalid" marker) also falls back.
  flatbuffers::FlatBufferBuilder builder2;
  auto rf2 = Fbs::CreateRFConfig(builder2, -1, true);
  TEST_ASSERT_TRUE(InitFrom(Finish(builder2, Fbs::CreateHubConfig(builder2, rf2))));
  TEST_ASSERT_TRUE(Config::GetRFConfigTxPin(txPin));
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, txPin);
}

TEST_CASE("jsonconfig set with an empty object resets to defaults", "[config][defaults][json]")
{
  TEST_ASSERT_TRUE(InitFrom(Serialize(MakeSampleConfig())));
  TEST_ASSERT_TRUE(Config::SaveFromJSON("{}"));

  // Except estop.active: an active E-Stop is never released by a JSON import (the sample's is active).
  Config::RootConfig expected;
  expected.estop.active = true;
  AssertConfigEqual(expected, Snapshot());

  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(expected, Snapshot());
}

TEST_CASE("jsonconfig set with partial sections fills in defaults", "[config][defaults][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"rf":{"txPin":13},"wifi":{"hostname":"my-hub","credentials":[]},"backend":{"domain":"api.example.org"},"otaUpdate":{"updateChannel":"develop"}})"));

  Config::RootConfig expected;
  expected.rf.txPin                = GPIO_NUM_13;
  expected.wifi.hostname           = "my-hub";
  expected.backend.domain          = "api.example.org";
  expected.otaUpdate.updateChannel = OtaUpdateChannel::Develop;
  AssertConfigEqual(expected, Snapshot());
}

TEST_CASE("jsonconfig set requires the wifi credentials array when wifi is given", "[config][defaults][json]")
{
  // Same rule as the develop firmware: a wifi object without "credentials" is rejected.
  InitDefault();
  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"wifi":{"hostname":"my-hub"}})"));
  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"wifi":{"hostname":"my-hub","credentials":{}}})"));
  TEST_ASSERT_EQUAL_INT(0, HostConfigFs::writeCount);
}
