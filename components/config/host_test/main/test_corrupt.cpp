// Corrupt input: a malformed credential blob or bad JSON over serial must
// be rejected without crashing, and must not replace the stored config.
#include <freertos/FreeRTOS.h>

#include "unity.h"

#include "config_test_helpers.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace OpenShock;
using namespace ConfigTest;

namespace {
  // Small deterministic PRNG (xorshift32) so failures are reproducible.
  uint32_t NextRandom(uint32_t& state)
  {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
}  // namespace

TEST_CASE("Every truncation and corruption of a credential blob is rejected without crashing", "[config][corrupt]")
{
  Config::WiFiCredentials sample(7, "Office", "s3cret!", WIFI_AUTH_WPA2_PSK);
  sample.bssid                    = {1, 2, 3, 4, 5, 6};
  const std::vector<uint8_t> blob = sample.ToBlob();

  Config::WiFiCredentials decoded;
  TEST_ASSERT_TRUE(decoded.FromBlob(blob));
  AssertCredentialsEqual(sample, decoded);

  for (std::size_t len = 0; len < blob.size(); ++len) {
    Config::WiFiCredentials out;
    std::vector<uint8_t> cut(blob.begin(), blob.begin() + len);
    char msg[48];
    snprintf(msg, sizeof(msg), "blob truncated to %zu bytes", len);
    TEST_ASSERT_FALSE_MESSAGE(out.FromBlob(cut), msg);
  }

  // Length prefixes past the end, an id of 0, an empty ssid
  std::vector<uint8_t> bad = blob;
  bad[9]                   = 200;
  TEST_ASSERT_FALSE(decoded.FromBlob(bad));
  bad    = blob;
  bad[1] = 0;
  TEST_ASSERT_FALSE(decoded.FromBlob(bad));
  bad    = blob;
  bad[9] = 0;
  TEST_ASSERT_FALSE(decoded.FromBlob(bad));

  uint32_t state = 0x9E3779B9;
  for (int round = 0; round < 200; ++round) {
    std::vector<uint8_t> data(NextRandom(state) % 300);
    for (auto& b : data) b = static_cast<uint8_t>(NextRandom(state));
    Config::WiFiCredentials out;
    out.FromBlob(data);  // must not crash; the result does not matter
  }
}

TEST_CASE("The credential blob's auth mode codes never change", "[config][corrupt]")
{
  // What hubs hold on flash: owned by the blob format, not by ESP-IDF or the FlatBuffers schema
  const std::pair<wifi_auth_mode_t, uint8_t> codes[] = {
    {           WIFI_AUTH_OPEN, 0},
    {            WIFI_AUTH_WEP, 1},
    {        WIFI_AUTH_WPA_PSK, 2},
    {       WIFI_AUTH_WPA2_PSK, 3},
    {   WIFI_AUTH_WPA_WPA2_PSK, 4},
    {WIFI_AUTH_WPA2_ENTERPRISE, 5},
    {       WIFI_AUTH_WPA3_PSK, 6},
    {  WIFI_AUTH_WPA2_WPA3_PSK, 7},
    {       WIFI_AUTH_WAPI_PSK, 8},
    {            WIFI_AUTH_MAX, 9},
  };
  for (const auto& [mode, code] : codes) {
    const auto blob = Config::WiFiCredentials(1, "Net", "pw", mode).ToBlob();
    TEST_ASSERT_EQUAL_UINT8(code, blob[2]);
    Config::WiFiCredentials decoded;
    TEST_ASSERT_TRUE(decoded.FromBlob(blob));
    TEST_ASSERT_EQUAL(mode, decoded.authMode);
  }
  // An auth mode the format has no code for is stored as unknown
  TEST_ASSERT_EQUAL_UINT8(9, Config::WiFiCredentials(1, "Net", "pw", WIFI_AUTH_OWE).ToBlob()[2]);
}

TEST_CASE("A version 1 blob with fields appended after the known ones still reads", "[config][corrupt]")
{
  // How the format grows: what a later firmware appends, an older one (after a rollback) skips
  Config::WiFiCredentials sample(7, "Office", "s3cret!", WIFI_AUTH_WPA2_PSK);
  std::vector<uint8_t> blob = sample.ToBlob();
  blob.insert(blob.end(), {0x01, 0x02, 0x03});
  Config::WiFiCredentials decoded;
  TEST_ASSERT_TRUE(decoded.FromBlob(blob));
  AssertCredentialsEqual(sample, decoded);
}

TEST_CASE("jsonconfig set rejects malformed JSON without touching the store", "[config][corrupt][json]")
{
  const char* inputs[] = {
    "",
    "   ",
    "not json",
    "{",
    "{\"rf\":",
    "[1,2,3]",
    "\"string\"",
    "42",
    "null",
    "{\"rf\":5}",
    "{\"rf\":{\"txPin\":13}",
    "{\"wifi\":{\"credentials\":[{\"id\":1,\"ssid\":\"x\"}]",
    "{\"estop\":{\"enabled\":\"yes\"}}",
    "{\"captivePortal\":[]}",
  };

  InitWith(MakeSampleConfig());
  const std::size_t before = UsedEntries();
  for (const char* input : inputs) {
    TEST_ASSERT_FALSE_MESSAGE(Config::SaveFromJSON(input), input);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(before, UsedEntries(), input);
    AssertConfigEqual(MakeSampleConfig(), Snapshot());
  }
  Reload();
  AssertConfigEqual(MakeSampleConfig(), Snapshot());
}

TEST_CASE("Rejected jsonconfig set leaves the running config unchanged", "[config][corrupt][json]")
{
  const Full sample = MakeSampleConfig();
  InitWith(sample);
  const std::size_t before = UsedEntries();

  // rf parses, then estop fails: the call must be all-or-nothing.
  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"rf":{"txPin":13,"keepAliveEnabled":true},"estop":{"enabled":"yes"}})"));
  TEST_ASSERT_EQUAL_size_t(before, UsedEntries());

  // Sections parsed before the failing one (rf) must not be applied either.
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("A malformed JSON bssid does not pin a partial address", "[config][corrupt][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"wifi":{"credentials":[{"id":1,"ssid":"net","bssid":"123456789aZZ"}]}})"));

  const Full loaded = Snapshot();
  TEST_ASSERT_EQUAL_size_t(1, loaded.wifi.credentialsList.size());
  TEST_ASSERT_FALSE(loaded.wifi.credentialsList[0].HasPinnedBSSID());
}

TEST_CASE("An unknown OTA channel in JSON falls back to the default, not the running value", "[config][corrupt][json]")
{
  Full sample                    = MakeSampleConfig();
  sample.otaUpdate.updateChannel = OtaUpdateChannel::Develop;
  InitWith(sample);

  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"otaUpdate":{"updateChannel":"nightly","updateStep":42}})"));

  const Full loaded = Snapshot();
  TEST_ASSERT_EQUAL_UINT8(std::to_underlying(OtaUpdateChannel::Stable), std::to_underlying(loaded.otaUpdate.updateChannel));
  TEST_ASSERT_EQUAL_UINT8(std::to_underlying(OtaUpdateStep::None), std::to_underlying(loaded.otaUpdate.updateStep));
}

TEST_CASE("A boolean field of the wrong type rejects the document instead of storing the default", "[config][corrupt][json]")
{
  Full sample                        = MakeSampleConfig();
  sample.captivePortal.alwaysEnabled = true;
  sample.serialInput.echoEnabled     = false;
  InitWith(sample);
  const std::size_t before = UsedEntries();

  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"captivePortal":{"alwaysEnabled":"false"}})"));
  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"serialInput":{"echoEnabled":1}})"));
  TEST_ASSERT_EQUAL_size_t(before, UsedEntries());
  AssertConfigEqual(sample, Snapshot());

  // A missing field is still its default
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"captivePortal":{},"serialInput":{}})"));
  TEST_ASSERT_FALSE(Snapshot().captivePortal.alwaysEnabled);
  TEST_ASSERT_TRUE(Snapshot().serialInput.echoEnabled);
}

TEST_CASE("A zero OTA check interval is normalized to the default on every path", "[config][corrupt][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"otaUpdate":{"checkPeriodically":true,"checkInterval":0}})"));
  TEST_ASSERT_EQUAL_UINT16(30, Snapshot().otaUpdate.checkInterval);

  TEST_ASSERT_TRUE(Config::UpdateOtaUpdateConfig([](Config::OtaUpdateConfig& cfg) {
    cfg.checkInterval = 0;
    return true;
  }));
  TEST_ASSERT_EQUAL_UINT16(30, Snapshot().otaUpdate.checkInterval);
  Reload();
  TEST_ASSERT_EQUAL_UINT16(30, Snapshot().otaUpdate.checkInterval);
}

TEST_CASE("An invalid hostname is normalized to the default on every path", "[config][corrupt][json]")
{
  InitDefault();
  for (const char* doc :
       {
         R"({"wifi":{"hostname":"","credentials":[]}})",
         R"({"wifi":{"hostname":"-hub","credentials":[]}})",
         R"({"wifi":{"hostname":"my hub","credentials":[]}})",
         R"({"wifi":{"hostname":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","credentials":[]}})",
       })
  {
    TEST_ASSERT_TRUE_MESSAGE(Config::SaveFromJSON(doc), doc);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(CONFIG_OPENSHOCK_FW_HOSTNAME, Snapshot().wifi.hostname.c_str(), doc);
  }

  TEST_ASSERT_TRUE(Config::SetWiFiHostname("hub_1"));
  TEST_ASSERT_EQUAL_STRING(CONFIG_OPENSHOCK_FW_HOSTNAME, Snapshot().wifi.hostname.c_str());
  TEST_ASSERT_TRUE(Config::SetWiFiHostname("hub-1"));
  TEST_ASSERT_EQUAL_STRING("hub-1", Snapshot().wifi.hostname.c_str());
  Reload();
  TEST_ASSERT_EQUAL_STRING("hub-1", Snapshot().wifi.hostname.c_str());
}

TEST_CASE("RemoveWiFiCredentials reports unknown IDs", "[config]")
{
  InitDefault();
  TEST_ASSERT_FALSE(Config::RemoveWiFiCredentials(42));
}

TEST_CASE("jsonconfig set survives deeply nested and huge input", "[config][corrupt][json]")
{
  InitDefault();

  std::string nested;
  for (int i = 0; i < 2000; ++i) nested += "{\"a\":";
  nested += "1";
  for (int i = 0; i < 2000; ++i) nested += "}";
  Config::SaveFromJSON(nested);  // must not crash; result does not matter

  std::string bigArray = "{\"wifi\":{\"credentials\":[";
  for (int i = 0; i < 2000; ++i) bigArray += (i ? ",1" : "1");
  bigArray += "]}}";
  Config::SaveFromJSON(bigArray);

  Snapshot();
}

TEST_CASE("Out-of-range JSON values are not stored", "[config][corrupt][json]")
{
  InitDefault();

  // txPin outside uint8 / not a real GPIO -> board default; id outside uint8 -> credentials rejected.
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"rf":{"txPin":300},"wifi":{"credentials":[{"id":1,"ssid":"ok"},{"id":"x","ssid":""}]},"otaUpdate":{"checkInterval":70000,"updateChannel":"nightly"}})"));

  const Full loaded = Snapshot();
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, loaded.rf.txPin);
  TEST_ASSERT_EQUAL_size_t(1, loaded.wifi.credentialsList.size());
  TEST_ASSERT_EQUAL_STRING("ok", loaded.wifi.credentialsList[0].ssid.c_str());
  TEST_ASSERT_EQUAL_UINT16(30, loaded.otaUpdate.checkInterval);
}
