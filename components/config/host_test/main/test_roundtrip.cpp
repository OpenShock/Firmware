// Round trips: a config written by this firmware must read back unchanged after a reboot, through the NVS store
// and through the jsonconfig (GetAsJSON / SaveFromJSON) path.
#include <freertos/FreeRTOS.h>

#include "unity.h"

#include "config_test_helpers.h"

#include <string>

using namespace OpenShock;
using namespace ConfigTest;

TEST_CASE("A stored config is loaded unchanged by Config::Init", "[config][roundtrip]")
{
  const Full sample = MakeSampleConfig();
  InitWith(sample);
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("Values set through the Config API survive a reload", "[config][roundtrip]")
{
  InitDefault();

  TEST_ASSERT_TRUE(Config::SetRFConfigTxPin(GPIO_NUM_25));
  TEST_ASSERT_TRUE(Config::SetRFConfigKeepAliveEnabled(false));
  TEST_ASSERT_EQUAL_UINT8(1, Config::AddWiFiCredentials("HomeNet", "hunter22", WIFI_AUTH_WPA2_PSK));
  TEST_ASSERT_EQUAL_UINT8(2, Config::AddWiFiCredentials("Guest", "", WIFI_AUTH_OPEN));
  const uint8_t bssid[6] = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  TEST_ASSERT_TRUE(Config::PinWiFiCredentialsBSSID(1, bssid));
  TEST_ASSERT_TRUE(Config::SetWiFiHostname("my-hub"));
  TEST_ASSERT_TRUE(Config::SetCaptivePortalConfig(Config::CaptivePortalConfig(true)));
  TEST_ASSERT_TRUE(Config::SetBackendDomain("api.example.org"));
  TEST_ASSERT_TRUE(Config::SetBackendAuthToken("tok_0123456789abcdef"));
  TEST_ASSERT_TRUE(Config::SetSerialInputConfigEchoEnabled(false));
  TEST_ASSERT_TRUE(Config::SetOtaUpdateId(4242));
  TEST_ASSERT_TRUE(Config::SetOtaUpdateStep(OtaUpdateStep::Updated));
  TEST_ASSERT_TRUE(Config::SetEStopGpioPin(GPIO_NUM_13));
  TEST_ASSERT_TRUE(Config::SetEStopEnabled(true));

  const Full before = Snapshot();
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, before.wifi.credentialsList[0].bssid.data(), 6);

  Reload();
  AssertConfigEqual(before, Snapshot());

  TEST_ASSERT_TRUE(Config::HasBackendAuthToken());
  auto token = Config::GetBackendAuthToken();
  TEST_ASSERT_TRUE(token.has_value());
  TEST_ASSERT_EQUAL_STRING("tok_0123456789abcdef", token->c_str());

  auto creds = Config::TryGetWiFiCredentialsBySSID("Guest");
  TEST_ASSERT_TRUE(creds.has_value());
  TEST_ASSERT_EQUAL_UINT8(2, creds->id);
  TEST_ASSERT_EQUAL_UINT8(2, Config::GetWiFiCredentialsIDbySSID("Guest"));
}

TEST_CASE("Whole-section setters round trip through the store", "[config][roundtrip]")
{
  const Full sample = MakeSampleConfig();
  InitDefault();

  TEST_ASSERT_TRUE(Config::SetRFConfig(sample.rf));
  TEST_ASSERT_TRUE(Config::SetWiFiConfig(sample.wifi));
  TEST_ASSERT_TRUE(Config::SetCaptivePortalConfig(sample.captivePortal));
  TEST_ASSERT_TRUE(Config::SetBackendConfig(sample.backend));
  TEST_ASSERT_TRUE(Config::SetSerialInputConfig(sample.serialInput));
  TEST_ASSERT_TRUE(Config::SetOtaUpdateConfig(sample.otaUpdate));
  TEST_ASSERT_TRUE(Config::SetEStopConfig(sample.estop));
  AssertConfigEqual(sample, Snapshot());

  Reload();
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("Removed credentials are gone after a reload and their ids can be reused", "[config][roundtrip]")
{
  InitDefault();
  TEST_ASSERT_EQUAL_UINT8(1, Config::AddWiFiCredentials("A", "pa", WIFI_AUTH_WPA2_PSK));
  TEST_ASSERT_EQUAL_UINT8(2, Config::AddWiFiCredentials("B", "pb", WIFI_AUTH_WPA2_PSK));
  TEST_ASSERT_EQUAL_UINT8(3, Config::AddWiFiCredentials("C", "pc", WIFI_AUTH_WPA2_PSK));
  TEST_ASSERT_TRUE(Config::RemoveWiFiCredentials(2));

  Reload();
  auto creds = Config::GetWiFiCredentials();
  TEST_ASSERT_TRUE(creds.has_value());
  TEST_ASSERT_EQUAL_size_t(2, creds->size());
  TEST_ASSERT_EQUAL_UINT8(1, (*creds)[0].id);
  TEST_ASSERT_EQUAL_UINT8(3, (*creds)[1].id);
  TEST_ASSERT_FALSE(Config::TryGetWiFiCredentialsByID(2).has_value());

  // The freed id is handed out again, and the list order is by id, not by insertion
  TEST_ASSERT_EQUAL_UINT8(2, Config::AddWiFiCredentials("D", "pd", WIFI_AUTH_WPA2_PSK));
  Reload();
  creds = Config::GetWiFiCredentials();
  TEST_ASSERT_EQUAL_size_t(3, creds->size());
  TEST_ASSERT_EQUAL_STRING("D", (*creds)[1].ssid.c_str());

  TEST_ASSERT_TRUE(Config::ClearWiFiCredentials());
  Reload();
  TEST_ASSERT_TRUE(Config::GetWiFiCredentials()->empty());
}

TEST_CASE("Credentials are enumerated from their keys, never counted", "[config][roundtrip]")
{
  // Ids with gaps and out of order, as a list that has been edited over time looks: everything comes back.
  Full config;
  config.wifi.credentialsList = {Config::WiFiCredentials(200, "High", "p1"), Config::WiFiCredentials(3, "Low", "p2"), Config::WiFiCredentials(255, "Max", "p3")};
  InitWith(config);

  auto creds = Config::GetWiFiCredentials();
  TEST_ASSERT_TRUE(creds.has_value());
  TEST_ASSERT_EQUAL_size_t(3, creds->size());
  TEST_ASSERT_EQUAL_UINT8(3, (*creds)[0].id);
  TEST_ASSERT_EQUAL_UINT8(200, (*creds)[1].id);
  TEST_ASSERT_EQUAL_UINT8(255, (*creds)[2].id);
  TEST_ASSERT_EQUAL_STRING("Max", (*creds)[2].ssid.c_str());
}

TEST_CASE("GetRaw writes fields that equal their schema default", "[config][roundtrip]")
{
  // 1.5.x reads a missing is_enabled/keepalive_enabled as false, so omitting them breaks loading the dump there.
  InitDefault();
  TEST_ASSERT_TRUE(Config::SetRFConfigKeepAliveEnabled(true));

  TinyVec<uint8_t> raw;
  TEST_ASSERT_TRUE(Config::GetRaw(raw));
  const auto* root = Serialization::Configuration::GetHubConfig(raw.data());
  // The generated tables derive from flatbuffers::Table privately; every table is one at the same address
  auto hasField = [](const void* table, flatbuffers::voffset_t field) { return static_cast<const flatbuffers::Table*>(table)->CheckField(field); };
  TEST_ASSERT_TRUE(hasField(root->rf(), Serialization::Configuration::RFConfig::VT_KEEPALIVE_ENABLED));
  TEST_ASSERT_TRUE(hasField(root->ota_update(), Serialization::Configuration::OtaUpdateConfig::VT_IS_ENABLED));
}

TEST_CASE("A save writes only what changed", "[config][roundtrip]")
{
  const Full sample = MakeSampleConfig();
  InitWith(sample);
  const std::size_t before = UsedEntries();

  // Setting what is already set changes nothing on flash
  TEST_ASSERT_TRUE(Config::SetWiFiConfig(sample.wifi));
  TEST_ASSERT_TRUE(Config::SetBackendConfig(sample.backend));
  TEST_ASSERT_TRUE(Config::SetOtaUpdateConfig(sample.otaUpdate));
  TEST_ASSERT_TRUE(Config::SetWiFiCredentials(sample.wifi.credentialsList));
  TEST_ASSERT_EQUAL_size_t(before, UsedEntries());

  // One scalar: one entry replaced (NVS writes the new one, then frees the old: the count does not grow)
  TEST_ASSERT_TRUE(Config::SetSerialInputConfigEchoEnabled(true));
  TEST_ASSERT_EQUAL_size_t(before, UsedEntries());
  Reload();
  TEST_ASSERT_TRUE(*Config::GetSerialInputConfigEchoEnabled());
}

TEST_CASE("Many max-length WiFi credentials survive a reload", "[config][roundtrip]")
{
  InitDefault();
  for (int i = 0; i < 16; ++i) {
    std::string ssid(32, static_cast<char>('A' + i));      // 802.11 SSID maximum
    std::string password(63, static_cast<char>('a' + i));  // WPA2 passphrase maximum
    TEST_ASSERT_EQUAL_UINT8(i + 1, Config::AddWiFiCredentials(ssid, password, WIFI_AUTH_WPA2_PSK));
  }
  const Full before = Snapshot();

  Reload();
  AssertConfigEqual(before, Snapshot());
}

TEST_CASE("The store holds more than the 1.x config file could", "[config][roundtrip]")
{
  // The 1.x firmware capped its config file at 4 KiB, some 30 credentials like these. cfg is that file's 12 KiB
  // partition retyped as NVS (three pages, one of them always kept free for compaction): 32 credentials with long
  // names and passphrases fit with room for the rest of the config. The store fills up at about 36.
  InitDefault();
  for (int i = 0; i < 32; ++i) {
    std::string ssid = "Network-" + std::to_string(i) + std::string(20, 'x');
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, Config::AddWiFiCredentials(ssid, std::string(63, 'p'), WIFI_AUTH_WPA2_PSK), ssid.c_str());
  }
  const Full before = Snapshot();
  TEST_ASSERT_EQUAL_size_t(32, before.wifi.credentialsList.size());
  Reload();
  AssertConfigEqual(before, Snapshot());
}

TEST_CASE("jsonconfig get/set round trip keeps every section", "[config][roundtrip][json]")
{
  Full sample = MakeSampleConfig();
  // Strings that need JSON escaping are covered separately below.
  sample.wifi.credentialsList[2] = Config::WiFiCredentials(7, "Office", "s3cret!", WIFI_AUTH_WPA3_PSK);
  InitWith(sample);
  const std::string json = Config::GetAsJSON(true);
  TEST_ASSERT_FALSE(json.empty());

  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(json));
  AssertConfigEqual(sample, Snapshot());

  // SaveFromJSON persists: the store reloads to the same config.
  Reload();
  AssertConfigEqual(sample, Snapshot());

  // And exporting again yields identical JSON.
  TEST_ASSERT_EQUAL_STRING(json.c_str(), Config::GetAsJSON(true).c_str());
}

TEST_CASE("jsonconfig round trip keeps strings that need escaping", "[config][roundtrip][json]")
{
  Full sample = MakeSampleConfig();
  InitWith(sample);
  const std::string json = Config::GetAsJSON(true);

  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(json));

  // Escaped exactly once on the way out (as the cJSON firmware did), decoded on the way in.
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find(R"("Office \"5G\"")"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find(R"("p@ss w0rd/\\")"));

  auto office = Config::TryGetWiFiCredentialsByID(7);
  TEST_ASSERT_TRUE(office.has_value());
  TEST_ASSERT_EQUAL_STRING("Office \"5G\"", office->ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("p@ss w0rd/\\", office->password.c_str());

  // Repeated get/set cycles are stable.
  TEST_ASSERT_TRUE(Config::SaveFromJSON(Config::GetAsJSON(true)));
  TEST_ASSERT_EQUAL_STRING(json.c_str(), Config::GetAsJSON(true).c_str());
}

TEST_CASE("jsonconfig set decodes \\u escapes", "[config][roundtrip][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"wifi":{"hostname":"hub-1","credentials":[{"id":1,"ssid":"Café 😀","password":"a\tb"}]}})"));

  Reload();  // UTF-8 and control characters survive the store too
  auto wifi = Config::GetWiFiConfig();
  TEST_ASSERT_TRUE(wifi.has_value());
  TEST_ASSERT_EQUAL_STRING("hub-1", wifi->hostname.c_str());
  TEST_ASSERT_EQUAL_size_t(1, wifi->credentialsList.size());
  TEST_ASSERT_EQUAL_STRING("Caf\xc3\xa9 \xf0\x9f\x98\x80", wifi->credentialsList[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("a\tb", wifi->credentialsList[0].password.c_str());
}

TEST_CASE("GetAsJSON without sensitive data omits secrets", "[config][roundtrip][json]")
{
  InitWith(MakeSampleConfig());

  const std::string full     = Config::GetAsJSON(true);
  const std::string redacted = Config::GetAsJSON(false);

  TEST_ASSERT_NOT_EQUAL(std::string::npos, full.find("hunter22"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, full.find("tok_0123456789abcdef"));
  TEST_ASSERT_EQUAL(std::string::npos, redacted.find("hunter22"));
  TEST_ASSERT_EQUAL(std::string::npos, redacted.find("password"));
  TEST_ASSERT_EQUAL(std::string::npos, redacted.find("authToken"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, redacted.find("\"HomeNet\""));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, redacted.find("\"api.example.org\""));
}

TEST_CASE("The wire form for the portal omits secrets and carries every section", "[config][roundtrip]")
{
  const Full sample = MakeSampleConfig();
  InitWith(sample);

  flatbuffers::FlatBufferBuilder builder;
  Serialization::Configuration::FinishHubConfigBuffer(builder, Config::GetAsFlatBuffer(builder, false));
  std::vector<uint8_t> exported(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());

  Full decoded;
  TEST_ASSERT_TRUE(Deserialize(exported.data(), exported.size(), decoded));
  AssertConfigEqual(sample, decoded, false);
}

TEST_CASE("UpdateOtaUpdateConfig edits only what the update touches and saves only on change", "[config][roundtrip]")
{
  const Full sample = MakeSampleConfig();
  InitWith(sample);

  const std::size_t before = UsedEntries();
  TEST_ASSERT_TRUE(Config::UpdateOtaUpdateConfig([](Config::OtaUpdateConfig&) { return false; }));
  TEST_ASSERT_EQUAL_size_t(before, UsedEntries());

  const uint16_t interval = static_cast<uint16_t>(sample.otaUpdate.checkInterval + 7);
  TEST_ASSERT_TRUE(Config::UpdateOtaUpdateConfig([&](Config::OtaUpdateConfig& cfg) {
    cfg.checkInterval = interval;
    return true;
  }));

  Full expected                    = sample;
  expected.otaUpdate.checkInterval = interval;
  AssertConfigEqual(expected, Snapshot());

  Reload();
  AssertConfigEqual(expected, Snapshot());
}

TEST_CASE("CompareExchangeOtaUpdateStep only replaces the expected step", "[config][roundtrip]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SetOtaUpdateStep(OtaUpdateStep::Updated));

  TEST_ASSERT_FALSE(Config::CompareExchangeOtaUpdateStep(OtaUpdateStep::Validated, OtaUpdateStep::None));
  TEST_ASSERT_TRUE(Config::GetOtaUpdateStep() == OtaUpdateStep::Updated);

  TEST_ASSERT_TRUE(Config::CompareExchangeOtaUpdateStep(OtaUpdateStep::Updated, OtaUpdateStep::None));
  TEST_ASSERT_TRUE(Config::GetOtaUpdateStep() == OtaUpdateStep::None);

  Reload();
  TEST_ASSERT_TRUE(Config::GetOtaUpdateStep() == OtaUpdateStep::None);
}

TEST_CASE("Factory reset wipes every setting and survives a reload", "[config][roundtrip]")
{
  InitWith(MakeSampleConfig());
  Config::FactoryReset();
  AssertConfigEqual(Full(), Snapshot());
  Reload();
  AssertConfigEqual(Full(), Snapshot());
}

TEST_CASE("rawconfig get and set round trip the whole config", "[config][roundtrip]")
{
  InitWith(MakeSampleConfig());
  TinyVec<uint8_t> raw;
  TEST_ASSERT_TRUE(Config::GetRaw(raw));
  const std::vector<uint8_t> exported(raw.data(), raw.data() + raw.size());

  InitDefault();
  TEST_ASSERT_TRUE(Config::SetRaw(exported.data(), exported.size()));
  AssertConfigEqual(MakeSampleConfig(), Snapshot());
  Reload();
  AssertConfigEqual(MakeSampleConfig(), Snapshot());

  // A buffer that does not verify changes nothing
  const std::size_t before = UsedEntries();
  TEST_ASSERT_FALSE(Config::SetRaw(exported.data(), exported.size() / 2));
  TEST_ASSERT_FALSE(Config::SetRaw(nullptr, 0));
  TEST_ASSERT_EQUAL_size_t(before, UsedEntries());
  AssertConfigEqual(MakeSampleConfig(), Snapshot());
}
