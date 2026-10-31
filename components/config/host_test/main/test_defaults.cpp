// Defaults: no stored config, a store missing namespaces or keys, must give usable defaults rather than a failed
// load, and a key this firmware does not know must be ignored.
#include <freertos/FreeRTOS.h>

#include "unity.h"

#include "config_test_helpers.h"

#include "BoardDescriptor.h"

#include <nvs_flash.h>

#include <string>
#include <utility>

using namespace OpenShock;
using namespace ConfigTest;
namespace Keys = ConfigTest::Keys;
namespace Nvs  = OpenShock::Config::Nvs;

namespace {
  // Writes a few keys straight into the store, as a different firmware version might have, then marks the schema
  // so Init loads rather than imports.
  Nvs::Handle Open(const char* ns)
  {
    auto handle = Nvs::Handle::Open(Keys::kPartitionCfg, ns, true);
    TEST_ASSERT_TRUE(handle.has_value());
    return std::move(*handle);
  }

  void MarkSchema()
  {
    auto root = Open(Keys::Ns::kRoot);
    TEST_ASSERT_TRUE(root.SetU8(Keys::Root::kSchema, Keys::kSchemaVersion));
    TEST_ASSERT_TRUE(root.Commit());
  }
}  // namespace

TEST_CASE("Default values come from Kconfig and the board descriptor", "[config][defaults]")
{
  // The host-test descriptor is compiled from openshock_board.h, so the macros are what Board::Get() serves.
  TEST_ASSERT_TRUE(OpenShock::Board::IsStamped());
  TEST_ASSERT_EQUAL_STRING(OPENSHOCK_FW_BOARD, OpenShock::Board::Get().board);
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, OpenShock::Board::Get().rfTxGpio);

  const Full config;

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
  TEST_ASSERT_EQUAL_UINT8(std::to_underlying(OtaUpdateChannel::Stable), std::to_underlying(config.otaUpdate.updateChannel));
  TEST_ASSERT_EQUAL_UINT16(30, config.otaUpdate.checkInterval);
  TEST_ASSERT_TRUE(config.otaUpdate.allowBackendManagement);
  TEST_ASSERT_EQUAL_INT32(0, config.otaUpdate.updateId);
  TEST_ASSERT_EQUAL_UINT8(std::to_underlying(OtaUpdateStep::None), std::to_underlying(config.otaUpdate.updateStep));
  // The host-test board has no E-Stop pin, so E-Stop defaults to off.
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_ESTOP_PIN, config.estop.gpioPin);
  TEST_ASSERT_FALSE(config.estop.enabled);
}

TEST_CASE("An empty store: Init writes a default config that reloads", "[config][defaults]")
{
  InitDefault();
  TEST_ASSERT_EQUAL_STRING("cfg", Config::StorageLabel());
  AssertConfigEqual(Full(), Snapshot());

  // Every section namespace and the schema marker are now on flash
  for (const char* ns : Keys::Ns::kAll) {
    TEST_ASSERT_TRUE_MESSAGE(NamespaceExists(Keys::kPartitionCfg, ns), ns);
  }
  TEST_ASSERT_TRUE(UsedEntries() > 0);

  Reload();
  AssertConfigEqual(Full(), Snapshot());
}

TEST_CASE("Missing sections default while present ones are kept", "[config][defaults]")
{
  WipeStore();
  {
    auto backend = Open(Keys::Ns::kBackend);
    TEST_ASSERT_TRUE(backend.SetStr(Keys::Backend::kDomain, "api.example.org"));
    TEST_ASSERT_TRUE(backend.SetStr(Keys::Backend::kToken, "tok"));
    TEST_ASSERT_TRUE(backend.Commit());
    auto estop = Open(Keys::Ns::kEstop);
    TEST_ASSERT_TRUE(estop.SetBool(Keys::Estop::kEnabled, true));
    TEST_ASSERT_TRUE(estop.SetI8(Keys::Estop::kGpio, 13));
    TEST_ASSERT_TRUE(estop.Commit());
  }
  MarkSchema();
  Reload();

  Full expected;
  expected.backend = Config::BackendConfig("api.example.org", "tok");
  expected.estop   = Config::EStopConfig(true, GPIO_NUM_13);
  AssertConfigEqual(expected, Snapshot());
}

TEST_CASE("Missing keys inside a section take their defaults", "[config][defaults]")
{
  WipeStore();
  {
    auto rf = Open(Keys::Ns::kRf);
    TEST_ASSERT_TRUE(rf.SetI8(Keys::Rf::kTxPin, 13));  // keepalive absent
    TEST_ASSERT_TRUE(rf.Commit());
    auto wifi = Open(Keys::Ns::kWifiCfg);
    TEST_ASSERT_TRUE(wifi.SetStr(Keys::WifiCfg::kHostname, "my-hub"));  // no AP SSID, no credentials namespace at all
    TEST_ASSERT_TRUE(wifi.Commit());
    auto backend = Open(Keys::Ns::kBackend);
    TEST_ASSERT_TRUE(backend.SetStr(Keys::Backend::kDomain, "api.example.org"));  // no auth token
    TEST_ASSERT_TRUE(backend.Commit());
    auto ota = Open(Keys::Ns::kOta);
    TEST_ASSERT_TRUE(ota.SetI32(Keys::Ota::kUpdateId, 77));  // the rest at their defaults
    TEST_ASSERT_TRUE(ota.Commit());
  }
  MarkSchema();
  Reload();
  const Full loaded = Snapshot();

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

TEST_CASE("Keys this firmware does not know are ignored and kept", "[config][defaults]")
{
  InitWith(MakeSampleConfig());
  {
    auto rf = Open(Keys::Ns::kRf);
    TEST_ASSERT_TRUE(rf.SetU16("future_fld", 1234));  // from a newer firmware
    TEST_ASSERT_TRUE(rf.Commit());
  }
  Reload();
  AssertConfigEqual(MakeSampleConfig(), Snapshot());
  TEST_ASSERT_TRUE(Config::SetRFConfigKeepAliveEnabled(false));  // a save of the section leaves it alone

  auto rf        = Open(Keys::Ns::kRf);
  uint16_t value = 0;
  TEST_ASSERT_EQUAL(Nvs::Result::Found, rf.GetU16("future_fld", value));
  TEST_ASSERT_EQUAL_UINT16(1234, value);
}

TEST_CASE("Malformed credential blobs are dropped, the rest kept", "[config][defaults]")
{
  Full config;
  config.wifi.credentialsList = {Config::WiFiCredentials(1, "HomeNet", "hunter22"), Config::WiFiCredentials(4, "Office", "")};
  InitWith(config);
  {
    auto wifi            = Open(Keys::Ns::kWifi);
    const uint8_t junk[] = {0x09, 0x02, 0x00};
    TEST_ASSERT_TRUE(wifi.SetBlob("2", junk));                        // unknown blob version
    TEST_ASSERT_TRUE(wifi.SetBlob("3", std::span<const uint8_t>()));  // empty
    TEST_ASSERT_TRUE(wifi.SetStr("notanid", "x"));                    // not a credential at all
    TEST_ASSERT_TRUE(wifi.Commit());
  }
  Reload();

  auto loaded = Config::GetWiFiCredentials();
  TEST_ASSERT_EQUAL_size_t(2, loaded.size());
  AssertCredentialsEqual(Config::WiFiCredentials(1, "HomeNet", "hunter22"), loaded[0]);
  AssertCredentialsEqual(Config::WiFiCredentials(4, "Office", ""), loaded[1]);
}

TEST_CASE("Invalid stored GPIO pins fall back safely", "[config][defaults]")
{
  WipeStore();
  {
    auto rf = Open(Keys::Ns::kRf);
    TEST_ASSERT_TRUE(rf.SetI8(Keys::Rf::kTxPin, 24));  // GPIO24 does not exist on ESP32
    TEST_ASSERT_TRUE(rf.Commit());
    auto estop = Open(Keys::Ns::kEstop);
    TEST_ASSERT_TRUE(estop.SetBool(Keys::Estop::kEnabled, true));
    TEST_ASSERT_TRUE(estop.SetI8(Keys::Estop::kGpio, 0));  // strapping pin: not a valid E-Stop input
    TEST_ASSERT_TRUE(estop.Commit());
  }
  MarkSchema();
  Reload();

  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, Config::GetRFConfigTxPin());
  TEST_ASSERT_FALSE(Config::GetEStopEnabled());

  // Negative pin (the old "invalid" marker) also falls back.
  WipeStore();
  {
    auto rf = Open(Keys::Ns::kRf);
    TEST_ASSERT_TRUE(rf.SetI8(Keys::Rf::kTxPin, -1));
    TEST_ASSERT_TRUE(rf.Commit());
  }
  MarkSchema();
  Reload();
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, Config::GetRFConfigTxPin());
}

TEST_CASE("jsonconfig set with an empty object resets to defaults", "[config][defaults][json]")
{
  InitWith(MakeSampleConfig());
  TEST_ASSERT_TRUE(Config::SaveFromJSON("{}"));
  AssertConfigEqual(Full(), Snapshot());

  Reload();
  AssertConfigEqual(Full(), Snapshot());
}

TEST_CASE("jsonconfig set with partial sections fills in defaults", "[config][defaults][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"rf":{"txPin":13},"wifi":{"hostname":"my-hub","credentials":[]},"backend":{"domain":"api.example.org"},"otaUpdate":{"updateChannel":"develop"}})"));

  Full expected;
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
  const std::size_t before = UsedEntries();
  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"wifi":{"hostname":"my-hub"}})"));
  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"wifi":{"hostname":"my-hub","credentials":{}}})"));
  TEST_ASSERT_EQUAL_size_t(before, UsedEntries());
}
