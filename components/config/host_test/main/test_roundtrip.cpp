// Round trips: a config written by this firmware must read back unchanged, both as
// the stored flatbuffer and through the jsonconfig (GetAsJSON / SaveFromJSON) path.
#include "unity.h"

#include "config_test_helpers.h"

#include <string>

using namespace OpenShock;
using namespace ConfigTest;

TEST_CASE("RootConfig flatbuffer round trip keeps every section", "[config][roundtrip]")
{
  const Config::RootConfig sample  = MakeSampleConfig();
  const std::vector<uint8_t> bytes = Serialize(sample);

  Config::RootConfig loaded;
  TEST_ASSERT_TRUE(Deserialize(bytes.data(), bytes.size(), loaded));
  AssertConfigEqual(sample, loaded);
}

TEST_CASE("Stored config is loaded unchanged by Config::Init", "[config][roundtrip]")
{
  const Config::RootConfig sample = MakeSampleConfig();

  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("Serializing without sensitive data drops only the secrets", "[config][roundtrip]")
{
  const Config::RootConfig sample  = MakeSampleConfig();
  const std::vector<uint8_t> bytes = Serialize(sample, false);

  Config::RootConfig loaded;
  TEST_ASSERT_TRUE(Deserialize(bytes.data(), bytes.size(), loaded));
  AssertConfigEqual(sample, loaded, false);
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

  const Config::RootConfig before = Snapshot();
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bssid, before.wifi.credentialsList[0].bssid.data(), 6);

  // Reboot: load the file the setters wrote.
  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(before, Snapshot());

  std::string token;
  TEST_ASSERT_TRUE(Config::HasBackendAuthToken());
  TEST_ASSERT_TRUE(Config::GetBackendAuthToken(token));
  TEST_ASSERT_EQUAL_STRING("tok_0123456789abcdef", token.c_str());

  Config::WiFiCredentials creds;
  TEST_ASSERT_TRUE(Config::TryGetWiFiCredentialsBySSID("Guest", creds));
  TEST_ASSERT_EQUAL_UINT8(2, creds.id);
  TEST_ASSERT_EQUAL_UINT8(2, Config::GetWiFiCredentialsIDbySSID("Guest"));
}

TEST_CASE("Whole-section setters round trip through the store", "[config][roundtrip]")
{
  const Config::RootConfig sample = MakeSampleConfig();
  InitDefault();

  TEST_ASSERT_TRUE(Config::SetRFConfig(sample.rf));
  TEST_ASSERT_TRUE(Config::SetWiFiConfig(sample.wifi));
  TEST_ASSERT_TRUE(Config::SetCaptivePortalConfig(sample.captivePortal));
  TEST_ASSERT_TRUE(Config::SetBackendConfig(sample.backend));
  TEST_ASSERT_TRUE(Config::SetSerialInputConfig(sample.serialInput));
  TEST_ASSERT_TRUE(Config::SetOtaUpdateConfig(sample.otaUpdate));
  TEST_ASSERT_TRUE(Config::SetEStopConfig(sample.estop));
  AssertConfigEqual(sample, Snapshot());

  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("GetAsFlatBuffer and SaveFromFlatBuffer round trip", "[config][roundtrip]")
{
  const Config::RootConfig sample = MakeSampleConfig();
  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));

  flatbuffers::FlatBufferBuilder builder;
  Serialization::Configuration::FinishHubConfigBuffer(builder, Config::GetAsFlatBuffer(builder, true));
  std::vector<uint8_t> exported(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());

  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromFlatBuffer(Serialization::Configuration::GetHubConfig(exported.data())));
  AssertConfigEqual(sample, Snapshot());

  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("SetRaw stores a valid buffer verbatim and GetRaw returns it", "[config][roundtrip]")
{
  const std::vector<uint8_t> bytes = Serialize(MakeSampleConfig());
  InitDefault();

  TEST_ASSERT_TRUE(Config::SetRaw(bytes.data(), bytes.size()));
  TEST_ASSERT_EQUAL_size_t(bytes.size(), StoredFile().size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), StoredFile().data(), bytes.size());

  TinyVec<uint8_t> raw;
  TEST_ASSERT_TRUE(Config::GetRaw(raw));
  TEST_ASSERT_EQUAL_size_t(bytes.size(), raw.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), raw.data(), bytes.size());

  // SetRaw takes effect immediately and survives a restart.
  AssertConfigEqual(MakeSampleConfig(), Snapshot());
  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(MakeSampleConfig(), Snapshot());
}

TEST_CASE("A setter after SetRaw keeps the imported config", "[config][roundtrip]")
{
  const std::vector<uint8_t> bytes = Serialize(MakeSampleConfig());
  InitDefault();

  TEST_ASSERT_TRUE(Config::SetRaw(bytes.data(), bytes.size()));
  TEST_ASSERT_TRUE(Config::SetSerialInputConfigEchoEnabled(!MakeSampleConfig().serialInput.echoEnabled));

  Config::RootConfig expected      = MakeSampleConfig();
  expected.serialInput.echoEnabled = !expected.serialInput.echoEnabled;
  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(expected, Snapshot());
}

TEST_CASE("Many max-length WiFi credentials survive a reload", "[config][roundtrip]")
{
  InitDefault();
  for (int i = 0; i < 16; ++i) {
    std::string ssid(32, static_cast<char>('A' + i));      // 802.11 SSID maximum
    std::string password(63, static_cast<char>('a' + i));  // WPA2 passphrase maximum
    TEST_ASSERT_EQUAL_UINT8(i + 1, Config::AddWiFiCredentials(ssid, password, WIFI_AUTH_WPA2_PSK));
  }
  const Config::RootConfig before = Snapshot();

  TEST_ASSERT_TRUE_MESSAGE(InitFrom(StoredFile()), "Init fell back to defaults");
  AssertConfigEqual(before, Snapshot());
}

TEST_CASE("A stored config is never too large to load again", "[config][roundtrip]")
{
  // AddWiFiCredentials hands out up to 255 IDs, but the config partition holds at
  // most Config::MaxConfigSize bytes: once another network would not fit, adding
  // is refused instead of storing a file that cannot be loaded again.
  InitDefault();
  int added = 0;
  for (int i = 0; i < 40; ++i) {
    std::string ssid                      = "Network-" + std::to_string(i) + std::string(20, 'x');
    const Config::RootConfig beforeAdd    = Snapshot();
    const std::vector<uint8_t> fileBefore = StoredFile();
    const int writesBefore                = HostConfigFs::writeCount;

    if (Config::AddWiFiCredentials(ssid, std::string(63, 'p'), WIFI_AUTH_WPA2_PSK) == 0) {
      // Refused: nothing changed, in memory or on flash.
      AssertConfigEqual(beforeAdd, Snapshot());
      TEST_ASSERT_EQUAL_INT(writesBefore, HostConfigFs::writeCount);
      TEST_ASSERT_TRUE(fileBefore == StoredFile());
      break;
    }
    ++added;
  }
  TEST_ASSERT_TRUE_MESSAGE(added >= 16, "far fewer credentials fit than expected");
  TEST_ASSERT_TRUE_MESSAGE(added < 40, "40 such credentials cannot fit in MaxConfigSize");
  TEST_ASSERT_LESS_OR_EQUAL_size_t(Config::MaxConfigSize, StoredFile().size());

  const Config::RootConfig before = Snapshot();
  TEST_ASSERT_EQUAL_size_t(added, before.wifi.credentialsList.size());
  TEST_ASSERT_TRUE_MESSAGE(InitFrom(StoredFile()), "Init fell back to defaults");
  AssertConfigEqual(before, Snapshot());
}

namespace {
  // A valid config whose serialized form is exactly `size` bytes (padded through the hostname).
  std::vector<uint8_t> SerializedOfSize(std::size_t size, Config::RootConfig& config)
  {
    config = MakeSampleConfig();
    for (std::size_t len = 0; len < size; ++len) {
      config.wifi.hostname.assign(len, 'h');
      std::vector<uint8_t> bytes = Serialize(config);
      if (bytes.size() == size) {
        return bytes;
      }
    }
    TEST_FAIL_MESSAGE("no hostname length gives that size");
    return {};
  }
}  // namespace

TEST_CASE("A config of exactly MaxConfigSize bytes loads", "[config][roundtrip]")
{
  Config::RootConfig sample;
  const std::vector<uint8_t> bytes = SerializedOfSize(Config::MaxConfigSize, sample);

  TEST_ASSERT_TRUE_MESSAGE(InitFrom(bytes), "Init fell back to defaults");
  AssertConfigEqual(sample, Snapshot());

  InitDefault();
  TEST_ASSERT_TRUE(Config::SetRaw(bytes.data(), bytes.size()));
  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("A config larger than MaxConfigSize is rejected, not aborted on", "[config][roundtrip]")
{
  // A well-formed flatbuffer, just too large: before the fix the Verifier
  // constructor asserted (size < max_size) and aborted the hub on every boot.
  Config::RootConfig sample;
  const std::vector<uint8_t> bytes = SerializedOfSize(Config::MaxConfigSize + 4, sample);

  InitDefault();
  TEST_ASSERT_FALSE(Config::SetRaw(bytes.data(), bytes.size()));
  TEST_ASSERT_EQUAL_INT(0, HostConfigFs::writeCount);

  TEST_ASSERT_FALSE(InitFrom(bytes));  // fell back to defaults and rewrote the file
  AssertConfigEqual(Config::RootConfig(), Snapshot());
  TEST_ASSERT_TRUE(InitFrom(StoredFile()));

  // Setters cannot store an oversized config either, and keep the old file.
  InitDefault();
  const std::vector<uint8_t> before = StoredFile();
  Config::WiFiConfig wifi           = sample.wifi;
  wifi.hostname.append(Config::MaxConfigSize, 'h');
  TEST_ASSERT_FALSE(Config::SetWiFiConfig(wifi));
  TEST_ASSERT_TRUE(before == StoredFile());
}

TEST_CASE("jsonconfig get/set round trip keeps every section", "[config][roundtrip][json]")
{
  Config::RootConfig sample = MakeSampleConfig();
  // Strings that need JSON escaping are covered separately below.
  sample.wifi.credentialsList[2] = Config::WiFiCredentials(7, "Office", "s3cret!", WIFI_AUTH_WPA3_PSK);
  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));
  const std::string json = Config::GetAsJSON(true);
  TEST_ASSERT_FALSE(json.empty());

  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(json));
  AssertConfigEqual(sample, Snapshot());

  // SaveFromJSON persists: the stored file reloads to the same config.
  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(sample, Snapshot());

  // And exporting again yields identical JSON.
  TEST_ASSERT_EQUAL_STRING(json.c_str(), Config::GetAsJSON(true).c_str());
}

TEST_CASE("jsonconfig round trip keeps strings that need escaping", "[config][roundtrip][json]")
{
  Config::RootConfig sample = MakeSampleConfig();
  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));
  const std::string json = Config::GetAsJSON(true);

  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(json));

  // Escaped exactly once on the way out (as the cJSON firmware did), decoded on the way in.
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find(R"("Office \"5G\"")"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, json.find(R"("p@ss w0rd/\\")"));

  Config::WiFiCredentials office;
  TEST_ASSERT_TRUE(Config::TryGetWiFiCredentialsByID(7, office));
  TEST_ASSERT_EQUAL_STRING("Office \"5G\"", office.ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("p@ss w0rd/\\", office.password.c_str());

  // Repeated get/set cycles are stable.
  TEST_ASSERT_TRUE(Config::SaveFromJSON(Config::GetAsJSON(true)));
  TEST_ASSERT_EQUAL_STRING(json.c_str(), Config::GetAsJSON(true).c_str());
}

TEST_CASE("jsonconfig set decodes \\u escapes", "[config][roundtrip][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"wifi":{"hostname":"hub\u002d1","credentials":[{"id":1,"ssid":"Caf\u00e9 \ud83d\ude00","password":"a\tb"}]}})"));

  Config::WiFiConfig wifi;
  TEST_ASSERT_TRUE(Config::GetWiFiConfig(wifi));
  TEST_ASSERT_EQUAL_STRING("hub-1", wifi.hostname.c_str());
  TEST_ASSERT_EQUAL_size_t(1, wifi.credentialsList.size());
  TEST_ASSERT_EQUAL_STRING("Caf\xc3\xa9 \xf0\x9f\x98\x80", wifi.credentialsList[0].ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("a\tb", wifi.credentialsList[0].password.c_str());
}

TEST_CASE("GetAsJSON without sensitive data omits secrets", "[config][roundtrip][json]")
{
  TEST_ASSERT_TRUE(InitFrom(Serialize(MakeSampleConfig())));

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
