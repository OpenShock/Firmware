// Corrupt input: truncated, garbage or tampered config buffers (from flash or from
// rawconfig/jsonconfig over serial) must be rejected without crashing, and must not
// replace the stored config.
#include "unity.h"

#include "config_test_helpers.h"
#include "legacy_fixtures.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
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

  // Loads a buffer through every path a corrupt config can arrive by. Returns
  // whether SetRaw accepted it; never crashes (that is the point of the callers).
  bool TryAllLoadPaths(const std::vector<uint8_t>& data)
  {
    InitDefault();
    const bool accepted = Config::SetRaw(data.data(), data.size());
    if (!accepted) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, HostConfigFs::writeCount, "rejected SetRaw must not write");
    }

    // Booting with it stored: Init either loads it or falls back to defaults.
    InitFrom(data);
    Snapshot();
    return accepted;
  }
}  // namespace

TEST_CASE("SetRaw rejects null and too-short buffers", "[config][corrupt]")
{
  InitDefault();
  const std::vector<uint8_t> before = StoredFile();

  const uint8_t tiny[3] = {0x04, 0x00, 0x00};
  TEST_ASSERT_FALSE(Config::SetRaw(nullptr, 0));
  TEST_ASSERT_FALSE(Config::SetRaw(nullptr, 128));
  TEST_ASSERT_FALSE(Config::SetRaw(tiny, 0));
  TEST_ASSERT_FALSE(Config::SetRaw(tiny, sizeof(tiny)));

  TEST_ASSERT_EQUAL_INT(0, HostConfigFs::writeCount);
  TEST_ASSERT_EQUAL_size_t(before.size(), StoredFile().size());
}

TEST_CASE("Every truncation of a legacy config is rejected", "[config][corrupt]")
{
  for (std::size_t len = 0; len < sizeof(Legacy::kDevelopConfig); ++len) {
    std::vector<uint8_t> data(Legacy::kDevelopConfig, Legacy::kDevelopConfig + len);
    char msg[48];
    snprintf(msg, sizeof(msg), "truncated to %zu bytes", len);
    TEST_ASSERT_FALSE_MESSAGE(TryAllLoadPaths(data), msg);
  }
}

TEST_CASE("Truncated config on flash boots with defaults and rewrites the file", "[config][corrupt]")
{
  const std::vector<uint8_t> truncated(Legacy::kDevelopConfig, Legacy::kDevelopConfig + sizeof(Legacy::kDevelopConfig) / 2);
  TEST_ASSERT_FALSE(InitFrom(truncated));  // fell back
  AssertConfigEqual(Config::RootConfig(), Snapshot());

  // The rewritten file is a valid default config.
  TEST_ASSERT_TRUE(InitFrom(StoredFile()));
  AssertConfigEqual(Config::RootConfig(), Snapshot());
}

TEST_CASE("Random garbage is rejected", "[config][corrupt]")
{
  uint32_t state = 0x12345678;
  for (int round = 0; round < 300; ++round) {
    std::vector<uint8_t> data(4 + NextRandom(state) % 600);
    for (auto& b : data) b = static_cast<uint8_t>(NextRandom(state));
    char msg[48];
    snprintf(msg, sizeof(msg), "garbage round %d", round);
    TEST_ASSERT_FALSE_MESSAGE(TryAllLoadPaths(data), msg);
  }
}

TEST_CASE("Bit flips in a legacy config never crash the loader", "[config][corrupt]")
{
  // A flipped byte may still form a valid (different) config; what matters is that
  // verification keeps every read in bounds (run under ASan/UBSan to see it).
  for (std::size_t i = 0; i < sizeof(Legacy::kDevelopConfig); ++i) {
    for (uint8_t mask : {0x01, 0x80, 0xFF}) {
      std::vector<uint8_t> data(Legacy::kDevelopConfig, Legacy::kDevelopConfig + sizeof(Legacy::kDevelopConfig));
      data[i] ^= mask;
      TryAllLoadPaths(data);
    }
  }
}

TEST_CASE("Out-of-range offsets in the root table are rejected", "[config][corrupt]")
{
  std::vector<uint8_t> data(Legacy::kDevelopConfig, Legacy::kDevelopConfig + sizeof(Legacy::kDevelopConfig));

  // Root offset pointing past the end of the buffer.
  data[0] = 0xF0;
  data[1] = 0xFF;
  TEST_ASSERT_FALSE(TryAllLoadPaths(data));

  // Root offset pointing at the last byte.
  const uint32_t last = static_cast<uint32_t>(data.size() - 1);
  data[0]             = static_cast<uint8_t>(last);
  data[1]             = static_cast<uint8_t>(last >> 8);
  TEST_ASSERT_FALSE(TryAllLoadPaths(data));
}

TEST_CASE("Unterminated and oversized strings are rejected", "[config][corrupt]")
{
  // Make the length prefix of the backend domain string ("api.example.org") absurd.
  std::vector<uint8_t> data(Legacy::kDevelopConfig, Legacy::kDevelopConfig + sizeof(Legacy::kDevelopConfig));
  const std::string needle = "api.example.org";
  auto it                  = std::search(data.begin(), data.end(), needle.begin(), needle.end());
  TEST_ASSERT_TRUE(it != data.end());
  const std::size_t lenPos = static_cast<std::size_t>(it - data.begin()) - 4;
  TEST_ASSERT_EQUAL_UINT8(needle.size(), data[lenPos]);

  data[lenPos + 1] = 0x7F;  // ~32 KiB long
  TEST_ASSERT_FALSE(TryAllLoadPaths(data));

  // Correct length, but the terminating NUL overwritten.
  data[lenPos + 1]                 = 0x00;
  data[lenPos + 4 + needle.size()] = 'X';
  TEST_ASSERT_FALSE(TryAllLoadPaths(data));
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

  for (const char* input : inputs) {
    TEST_ASSERT_TRUE(InitFrom(Serialize(MakeSampleConfig())));
    const std::vector<uint8_t> before = StoredFile();

    TEST_ASSERT_FALSE_MESSAGE(Config::SaveFromJSON(input), input);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, HostConfigFs::writeCount, input);
    TEST_ASSERT_EQUAL_size_t(before.size(), StoredFile().size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(before.data(), StoredFile().data(), before.size());
  }
}

TEST_CASE("Rejected jsonconfig set leaves the running config unchanged", "[config][corrupt][json]")
{
  const Config::RootConfig sample = MakeSampleConfig();
  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));

  // rf parses, then estop fails: the call must be all-or-nothing.
  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"rf":{"txPin":13,"keepAliveEnabled":true},"estop":{"enabled":"yes"}})"));
  TEST_ASSERT_EQUAL_INT(0, HostConfigFs::writeCount);

  // Sections parsed before the failing one (rf) must not be applied either.
  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("jsonconfig set that cannot be stored leaves the running config unchanged", "[config][corrupt][json]")
{
  const Config::RootConfig sample = MakeSampleConfig();
  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));
  const std::vector<uint8_t> before = StoredFile();

  HostConfigFs::failWrites = true;
  TEST_ASSERT_FALSE(Config::SaveFromJSON(R"({"rf":{"txPin":13,"keepAliveEnabled":true}})"));
  HostConfigFs::failWrites = false;

  AssertConfigEqual(sample, Snapshot());
  TEST_ASSERT_TRUE(before == StoredFile());
}

TEST_CASE("Adding WiFi credentials that cannot be stored changes nothing", "[config][corrupt]")
{
  const Config::RootConfig sample = MakeSampleConfig();
  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));

  HostConfigFs::failWrites = true;
  TEST_ASSERT_EQUAL_UINT8(0, Config::AddWiFiCredentials("NewNet", "password1", WIFI_AUTH_WPA2_PSK));  // new SSID
  TEST_ASSERT_EQUAL_UINT8(0, Config::AddWiFiCredentials("HomeNet", "changed!", WIFI_AUTH_WPA3_PSK));  // existing SSID
  HostConfigFs::failWrites = false;

  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("Setters that cannot be stored leave the running config unchanged", "[config][corrupt]")
{
  const Config::RootConfig sample = MakeSampleConfig();
  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));
  TEST_ASSERT_FALSE(sample.wifi.credentialsList.empty());

  HostConfigFs::failWrites = true;
  TEST_ASSERT_FALSE(Config::SetBackendAuthToken("replaced-token"));
  TEST_ASSERT_FALSE(Config::ClearBackendAuthToken());
  TEST_ASSERT_FALSE(Config::SetWiFiHostname("other-host"));
  TEST_ASSERT_FALSE(Config::SetRFConfigKeepAliveEnabled(!sample.rf.keepAliveEnabled));
  TEST_ASSERT_FALSE(Config::SetEStopEnabled(!sample.estop.enabled));
  TEST_ASSERT_FALSE(Config::ClearWiFiCredentials());
  TEST_ASSERT_FALSE(Config::RemoveWiFiCredentials(sample.wifi.credentialsList.front().id));
  HostConfigFs::failWrites = false;

  AssertConfigEqual(sample, Snapshot());
}

TEST_CASE("A malformed JSON bssid does not pin a partial address", "[config][corrupt][json]")
{
  InitDefault();
  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"wifi":{"credentials":[{"id":1,"ssid":"net","bssid":"123456789aZZ"}]}})"));

  const Config::RootConfig loaded = Snapshot();
  TEST_ASSERT_EQUAL_size_t(1, loaded.wifi.credentialsList.size());
  TEST_ASSERT_FALSE(loaded.wifi.credentialsList[0].HasPinnedBSSID());
}

TEST_CASE("An unknown OTA channel in JSON falls back to the default, not the running value", "[config][corrupt][json]")
{
  Config::RootConfig sample      = MakeSampleConfig();
  sample.otaUpdate.updateChannel = OtaUpdateChannel::Develop;
  TEST_ASSERT_TRUE(InitFrom(Serialize(sample)));

  TEST_ASSERT_TRUE(Config::SaveFromJSON(R"({"otaUpdate":{"updateChannel":"nightly","updateStep":42}})"));

  const Config::RootConfig loaded = Snapshot();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaUpdateChannel::Stable), static_cast<uint8_t>(loaded.otaUpdate.updateChannel));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(OtaUpdateStep::None), static_cast<uint8_t>(loaded.otaUpdate.updateStep));
}

TEST_CASE("Schema fields the firmware does not model are dropped on save", "[config][corrupt]")
{
  // estop.active / estop.latching (and wifi AP/LAN fields) exist in the schema but nothing in the firmware reads
  // them, so they are intentionally not round-tripped. This pins that behaviour.
  namespace Fbs = Serialization::Configuration;
  flatbuffers::FlatBufferBuilder builder;
  auto estop = Fbs::CreateEStopConfig(builder, false, -1, /*active=*/true, /*latching=*/true);
  Fbs::FinishHubConfigBuffer(builder, Fbs::CreateHubConfig(builder, 0, 0, 0, 0, 0, 0, estop));
  TEST_ASSERT_TRUE(InitFrom(builder.GetBufferPointer(), builder.GetSize()));

  TEST_ASSERT_TRUE(Config::SetSerialInputConfigEchoEnabled(false));  // any save rewrites the file

  const std::vector<uint8_t> stored = StoredFile();
  const auto* root                  = Fbs::GetHubConfig(stored.data());
  TEST_ASSERT_NOT_NULL(root->estop());
  TEST_ASSERT_FALSE(root->estop()->active());
  TEST_ASSERT_FALSE(root->estop()->latching());
}

TEST_CASE("RemoveWiFiCredentials reports unknown IDs", "[config]")
{
  InitDefault();
  TEST_ASSERT_FALSE(Config::RemoveWiFiCredentials(42));
}

TEST_CASE("Out-of-range E-Stop GPIO in a stored config falls back to the default", "[config][corrupt]")
{
  // gpio_pin is an int8 on flash; values outside gpio_num_t must not be cast to it
  // (undefined behaviour, flagged by UBSan) and mean a corrupt section.
  namespace Fbs = Serialization::Configuration;
  for (int8_t pin : {static_cast<int8_t>(-128), static_cast<int8_t>(-2), static_cast<int8_t>(GPIO_NUM_MAX), static_cast<int8_t>(127)}) {
    flatbuffers::FlatBufferBuilder builder;
    auto estop = Fbs::CreateEStopConfig(builder, true, pin);
    auto root  = Fbs::CreateHubConfig(builder, 0, 0, 0, 0, 0, 0, estop);
    Fbs::FinishHubConfigBuffer(builder, root);

    char msg[32];
    snprintf(msg, sizeof(msg), "gpio_pin %d", pin);
    TEST_ASSERT_TRUE_MESSAGE(InitFrom(builder.GetBufferPointer(), builder.GetSize()), msg);

    Config::EStopConfig estopConfig;
    TEST_ASSERT_TRUE(Config::GetEStop(estopConfig));
    TEST_ASSERT_EQUAL_INT_MESSAGE(Config::EStopConfig().gpioPin, estopConfig.gpioPin, msg);
    TEST_ASSERT_EQUAL_MESSAGE(Config::EStopConfig().enabled, estopConfig.enabled, msg);
  }
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

  const Config::RootConfig loaded = Snapshot();
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, loaded.rf.txPin);
  TEST_ASSERT_EQUAL_size_t(1, loaded.wifi.credentialsList.size());
  TEST_ASSERT_EQUAL_STRING("ok", loaded.wifi.credentialsList[0].ssid.c_str());
  TEST_ASSERT_EQUAL_UINT16(30, loaded.otaUpdate.checkInterval);
}
