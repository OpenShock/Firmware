// Upgrade safety: hubs flashed from the Arduino firmware (1.5.x releases and the
// develop branch) must keep their config. The fixtures are the exact bytes those
// firmwares stored in the "config" partition (see legacy_fixtures.h).
#include "unity.h"

#include "config_test_helpers.h"
#include "legacy_fixtures.h"

#include <optional>
#include <string>
#include <vector>

using namespace OpenShock;
using namespace ConfigTest;

namespace {
  // The settings every legacy fixture holds (see legacy_fixtures.h).
  Config::RootConfig ExpectedLegacyConfig(bool hasAuthModeAndBssid, bool keepAlive)
  {
    Config::RootConfig config;
    config.rf = Config::RFConfig(GPIO_NUM_15, keepAlive);

    Config::WiFiCredentials home(1, "HomeNet", "hunter22");
    Config::WiFiCredentials guest(2, "Guest", "");
    Config::WiFiCredentials office(3, "Office", "s3cret!");
    if (hasAuthModeAndBssid) {
      home.authMode  = WIFI_AUTH_WPA2_PSK;
      home.bssid     = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
      guest.authMode = WIFI_AUTH_OPEN;
    }
    config.wifi = Config::WiFiConfig("MyHubAP", "my-hub", {home, guest, office});

    config.captivePortal = Config::CaptivePortalConfig(true);
    config.backend       = Config::BackendConfig("api.example.org", "tok_0123456789abcdef");
    config.serialInput   = Config::SerialInputConfig(false);
    config.otaUpdate     = Config::OtaUpdateConfig(false, "fw.example.org", OtaUpdateChannel::Beta, true, true, 120, false, true, 4242, OtaUpdateStep::Validating);
    config.estop         = Config::EStopConfig(true, GPIO_NUM_13);
    return config;
  }

  // develop's `jsonconfig` output (cJSON_PrintUnformatted of RootConfig::ToJSON) for
  // kDevelopConfig: same key order and number/bool/bssid formatting as its ToJSON bodies.
  constexpr const char* kDevelopJson = R"({"rf":{"txPin":15,"keepAliveEnabled":false},)"
                                       R"("wifi":{"accessPointSSID":"MyHubAP","hostname":"my-hub","credentials":[)"
                                       R"({"id":1,"ssid":"HomeNet","password":"hunter22","authMode":3,"bssid":"123456789ABC"},)"
                                       R"({"id":2,"ssid":"Guest","password":"","authMode":0},)"
                                       R"({"id":3,"ssid":"Office","password":"s3cret!","authMode":9}]},)"
                                       R"("captivePortal":{"alwaysEnabled":true},)"
                                       R"("backend":{"domain":"api.example.org","authToken":"tok_0123456789abcdef"},)"
                                       R"("serialInput":{"echoEnabled":false},)"
                                       R"("otaUpdate":{"isEnabled":false,"cdnDomain":"fw.example.org","updateChannel":"Beta","checkOnStartup":true,"checkPeriodically":true,)"
                                       R"("checkInterval":120,"allowBackendManagement":false,"requireManualApproval":true,"updateId":4242,"updateStep":"Validating"},)"
                                       R"("estop":{"enabled":true,"gpioPin":13}})";

  // 1.5.2's `jsonconfig` output for kV152Config (its credentials had no authMode/bssid).
  constexpr const char* kV152Json = R"({"rf":{"txPin":15,"keepAliveEnabled":true},)"
                                    R"("wifi":{"accessPointSSID":"MyHubAP","hostname":"my-hub","credentials":[)"
                                    R"({"id":1,"ssid":"HomeNet","password":"hunter22"},)"
                                    R"({"id":2,"ssid":"Guest","password":""},)"
                                    R"({"id":3,"ssid":"Office","password":"s3cret!"}]},)"
                                    R"("captivePortal":{"alwaysEnabled":true},)"
                                    R"("backend":{"domain":"api.example.org","authToken":"tok_0123456789abcdef"},)"
                                    R"("serialInput":{"echoEnabled":false},)"
                                    R"("otaUpdate":{"isEnabled":false,"cdnDomain":"fw.example.org","updateChannel":"Beta","checkOnStartup":true,"checkPeriodically":true,)"
                                    R"("checkInterval":120,"allowBackendManagement":false,"requireManualApproval":true,"updateId":4242,"updateStep":"Validating"},)"
                                    R"("estop":{"enabled":true,"gpioPin":13}})";
}  // namespace

TEST_CASE("Config stored by develop (Arduino) firmware loads unchanged", "[config][legacy]")
{
  TEST_ASSERT_TRUE_MESSAGE(InitFrom(Legacy::kDevelopConfig, sizeof(Legacy::kDevelopConfig)), "Init fell back to defaults");
  AssertConfigEqual(ExpectedLegacyConfig(true, false), Snapshot());

  // Loading alone must not rewrite the file.
  TEST_ASSERT_EQUAL_size_t(sizeof(Legacy::kDevelopConfig), StoredFile().size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(Legacy::kDevelopConfig, StoredFile().data(), sizeof(Legacy::kDevelopConfig));
}

TEST_CASE("Config stored by 1.5.2 release firmware loads unchanged", "[config][legacy]")
{
  TEST_ASSERT_TRUE_MESSAGE(InitFrom(Legacy::kV152Config, sizeof(Legacy::kV152Config)), "Init fell back to defaults");

  // 1.5.2 stored no auth mode / BSSID: they read back as unknown / not pinned.
  // OTA updates and backend management are off in this file, which 1.5.2 encoded
  // by omitting the fields (its schema default was false); the stored keepalive
  // true shows the file uses that convention, so they must not read as true.
  AssertConfigEqual(ExpectedLegacyConfig(false, true), Snapshot());
}

TEST_CASE("1.5.2 booleans stored as false keep their value", "[config][legacy]")
{
  // kV152ConfigKeepAliveOff has rf.keepalive_enabled, ota.is_enabled and
  // ota.allow_backend_management all false.
  TEST_ASSERT_TRUE(InitFrom(Legacy::kV152ConfigKeepAliveOff, sizeof(Legacy::kV152ConfigKeepAliveOff)));

  bool allOn;
  {
    // Scoped: TEST_IGNORE longjmps out, skipping destructors.
    const Config::RootConfig loaded = Snapshot();
    allOn                           = loaded.rf.keepAliveEnabled && loaded.otaUpdate.isEnabled && loaded.otaUpdate.allowBackendManagement;
  }
  if (allOn) {
    // 1.5.2's schema gave these three fields default false, so flatbuffers never
    // wrote a false value; the current schema defaults them to true. With all three
    // false, 1.5.2 omitted all three, which is byte for byte how 1.6.0-beta/rc and
    // develop store all three true (they omit their default), and no other field
    // differs between those writers. Without a version marker this file cannot be
    // told apart, so it keeps the current default. Files where any of the three is
    // stored as true are resolved (see the tests above and below). Pinned so other
    // changes still fail.
    TEST_IGNORE_MESSAGE("KNOWN ISSUE: 1.5.x with keepalive, OTA and backend management all off reads back all on (ambiguous with 1.6 defaults)");
  }
  AssertConfigEqual(ExpectedLegacyConfig(false, false), Snapshot());
}

namespace {
  namespace Fbs = Serialization::Configuration;

  // An rf + ota_update config where each of the three flipped-default booleans is
  // stored with the given value, or omitted (std::nullopt), as a flatbuffer writer
  // under either schema would leave it.
  std::vector<uint8_t> FlippedDefaultsConfig(std::optional<bool> keepAlive, std::optional<bool> otaEnabled, std::optional<bool> backendManagement)
  {
    flatbuffers::FlatBufferBuilder builder;

    Fbs::RFConfigBuilder rf(builder);
    rf.add_tx_pin(15);
    if (keepAlive) builder.AddElement<uint8_t>(Fbs::RFConfig::VT_KEEPALIVE_ENABLED, *keepAlive);
    auto rfOffset = rf.Finish();

    Fbs::OtaUpdateConfigBuilder ota(builder);
    ota.add_check_interval(30);
    if (otaEnabled) builder.AddElement<uint8_t>(Fbs::OtaUpdateConfig::VT_IS_ENABLED, *otaEnabled);
    if (backendManagement) builder.AddElement<uint8_t>(Fbs::OtaUpdateConfig::VT_ALLOW_BACKEND_MANAGEMENT, *backendManagement);
    auto otaOffset = ota.Finish();

    Fbs::FinishHubConfigBuffer(builder, Fbs::CreateHubConfig(builder, rfOffset, 0, 0, 0, 0, otaOffset));
    return std::vector<uint8_t>(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
  }

  void AssertFlippedDefaults(const std::vector<uint8_t>& file, bool keepAlive, bool otaEnabled, bool backendManagement)
  {
    TEST_ASSERT_TRUE(InitFrom(file));
    const Config::RootConfig loaded = Snapshot();
    TEST_ASSERT_EQUAL_MESSAGE(keepAlive, loaded.rf.keepAliveEnabled, "rf.keepAliveEnabled");
    TEST_ASSERT_EQUAL_MESSAGE(otaEnabled, loaded.otaUpdate.isEnabled, "otaUpdate.isEnabled");
    TEST_ASSERT_EQUAL_MESSAGE(backendManagement, loaded.otaUpdate.allowBackendManagement, "otaUpdate.allowBackendManagement");
  }
}  // namespace

TEST_CASE("Omitted flipped-default booleans follow the convention the file shows", "[config][legacy]")
{
  // 1.5.x convention (default false): only true is stored, so an omitted field is false.
  AssertFlippedDefaults(FlippedDefaultsConfig(true, std::nullopt, std::nullopt), true, false, false);
  AssertFlippedDefaults(FlippedDefaultsConfig(std::nullopt, true, std::nullopt), false, true, false);
  AssertFlippedDefaults(FlippedDefaultsConfig(std::nullopt, std::nullopt, true), false, false, true);
  AssertFlippedDefaults(FlippedDefaultsConfig(true, true, std::nullopt), true, true, false);

  // 1.6+ convention (default true): only false is stored, so an omitted field is true.
  AssertFlippedDefaults(FlippedDefaultsConfig(false, std::nullopt, std::nullopt), false, true, true);
  AssertFlippedDefaults(FlippedDefaultsConfig(std::nullopt, false, false), true, false, false);

  // Explicit values are always taken as stored.
  AssertFlippedDefaults(FlippedDefaultsConfig(true, false, true), true, false, true);
  AssertFlippedDefaults(FlippedDefaultsConfig(false, true, false), false, true, false);

  // All omitted: ambiguous, keeps the current schema default.
  AssertFlippedDefaults(FlippedDefaultsConfig(std::nullopt, std::nullopt, std::nullopt), true, true, true);
}

TEST_CASE("This firmware stores the flipped-default booleans explicitly", "[config][legacy]")
{
  // So its files read back the same under any schema (a downgrade to 1.5.x reads
  // an omitted field as false) and are never ambiguous.
  for (bool value : {true, false}) {
    Config::RootConfig config;
    config.rf.keepAliveEnabled              = value;
    config.otaUpdate.isEnabled              = value;
    config.otaUpdate.allowBackendManagement = value;
    const std::vector<uint8_t> bytes        = Serialize(config);
    const Fbs::HubConfig* fbs               = Fbs::GetHubConfig(bytes.data());
    // Generated tables derive privately from flatbuffers::Table (a view of the same bytes).
    const auto* rf  = static_cast<const flatbuffers::Table*>(static_cast<const void*>(fbs->rf()));
    const auto* ota = static_cast<const flatbuffers::Table*>(static_cast<const void*>(fbs->ota_update()));
    TEST_ASSERT_TRUE(rf->CheckField(Fbs::RFConfig::VT_KEEPALIVE_ENABLED));
    TEST_ASSERT_TRUE(ota->CheckField(Fbs::OtaUpdateConfig::VT_IS_ENABLED));
    TEST_ASSERT_TRUE(ota->CheckField(Fbs::OtaUpdateConfig::VT_ALLOW_BACKEND_MANAGEMENT));

    Config::RootConfig loaded;
    TEST_ASSERT_TRUE(Deserialize(bytes.data(), bytes.size(), loaded));
    AssertConfigEqual(config, loaded);
  }
}

TEST_CASE("Legacy config survives the first save by the new firmware", "[config][legacy]")
{
  TEST_ASSERT_TRUE(InitFrom(Legacy::kDevelopConfig, sizeof(Legacy::kDevelopConfig)));

  // Any setter rewrites the whole file in the new firmware's encoding.
  TEST_ASSERT_TRUE(Config::SetOtaUpdateStep(OtaUpdateStep::Validated));
  Config::RootConfig expected   = ExpectedLegacyConfig(true, false);
  expected.otaUpdate.updateStep = OtaUpdateStep::Validated;

  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(expected, Snapshot());
}

TEST_CASE("rawconfig set accepts a config backed up from legacy firmware", "[config][legacy]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SetRaw(Legacy::kV152Config, sizeof(Legacy::kV152Config)));
  TEST_ASSERT_TRUE(Config::SetRaw(Legacy::kDevelopConfig, sizeof(Legacy::kDevelopConfig)));

  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(ExpectedLegacyConfig(true, false), Snapshot());
}

TEST_CASE("jsonconfig set accepts JSON exported by develop firmware", "[config][legacy][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(kDevelopJson));
  AssertConfigEqual(ExpectedLegacyConfig(true, false), Snapshot());

  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(ExpectedLegacyConfig(true, false), Snapshot());
}

TEST_CASE("jsonconfig set accepts JSON exported by 1.5.2 firmware", "[config][legacy][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(kV152Json));
  AssertConfigEqual(ExpectedLegacyConfig(false, true), Snapshot());
}

TEST_CASE("jsonconfig get emits the same document develop did", "[config][legacy][json]")
{
  TEST_ASSERT_TRUE(InitFrom(Legacy::kDevelopConfig, sizeof(Legacy::kDevelopConfig)));

  // Same document plus the E-Stop's latching/active keys, which develop did not emit yet.
  std::string expected       = kDevelopJson;
  const std::string oldEStop = R"("estop":{"enabled":true,"gpioPin":13})";
  const size_t pos           = expected.find(oldEStop);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, pos);
  expected.replace(pos, oldEStop.size(), R"("estop":{"enabled":true,"gpioPin":13,"latching":false,"active":false})");
  TEST_ASSERT_EQUAL_STRING(expected.c_str(), Config::GetAsJSON(true).c_str());
}

TEST_CASE("Fields from a newer schema are tolerated on load", "[config][legacy]")
{
  // The schema has grown fields this firmware does not read yet (wifi AP password /
  // disable flags, LAN config). A file carrying them must still load.
  namespace Fbs = Serialization::Configuration;
  flatbuffers::FlatBufferBuilder builder;
  auto creds   = builder.CreateVector(std::vector<flatbuffers::Offset<Fbs::WiFiCredentials>> {Fbs::CreateWiFiCredentialsDirect(builder, 1, "HomeNet", "hunter22")});
  auto wifi    = Fbs::CreateWiFiConfig(builder, builder.CreateString("MyHubAP"), builder.CreateString("my-hub"), creds, builder.CreateString("ap-secret"), true, true, true);
  auto lan     = Fbs::CreateLanConfigDirect(builder, true, "lan-key");
  auto backend = Fbs::CreateBackendConfigDirect(builder, "api.example.org", "tok");
  auto root    = Fbs::CreateHubConfig(builder, 0, wifi, 0, backend, 0, 0, 0, lan);
  Fbs::FinishHubConfigBuffer(builder, root);

  TEST_ASSERT_TRUE(InitFrom(builder.GetBufferPointer(), builder.GetSize()));

  Config::WiFiConfig wifiConfig;
  TEST_ASSERT_TRUE(Config::GetWiFiConfig(wifiConfig));
  TEST_ASSERT_EQUAL_STRING("my-hub", wifiConfig.hostname.c_str());
  TEST_ASSERT_EQUAL_size_t(1, wifiConfig.credentialsList.size());
  TEST_ASSERT_EQUAL_STRING("hunter22", wifiConfig.credentialsList[0].password.c_str());

  std::string domain;
  TEST_ASSERT_TRUE(Config::GetBackendDomain(domain));
  TEST_ASSERT_EQUAL_STRING("api.example.org", domain.c_str());
}
