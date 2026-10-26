// Corrupt input: a truncated, garbage or tampered 1.x config file, a malformed credential blob, or bad JSON over
// serial must be rejected without crashing, and must not replace the stored config.
#include <freertos/FreeRTOS.h>

#include "unity.h"

#include "config_test_helpers.h"
#include "legacy_fixtures.h"

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

  // Runs a 1.x config blob through the import: the parser must reject it, and booting with it as the file must
  // fall back to the defaults. Returns whether the parser accepted it; never crashes (that is the point).
  bool TryImport(const std::vector<uint8_t>& data)
  {
    Full parsed;
    const bool accepted = Deserialize(data.data(), data.size(), parsed);
    const bool imported = InitFromLegacy(data);
    TEST_ASSERT_EQUAL_MESSAGE(accepted, imported, "the import and the parser must agree");
    Snapshot();
    return accepted;
  }
}  // namespace

TEST_CASE("Every truncation of a legacy config is rejected", "[config][corrupt]")
{
  for (std::size_t len = 0; len < sizeof(Legacy::kDevelopConfig); ++len) {
    std::vector<uint8_t> data(Legacy::kDevelopConfig, Legacy::kDevelopConfig + len);
    Full parsed;
    char msg[48];
    snprintf(msg, sizeof(msg), "truncated to %zu bytes", len);
    TEST_ASSERT_FALSE_MESSAGE(Deserialize(data.data(), data.size(), parsed), msg);
  }
}

TEST_CASE("A truncated 1.x file boots with defaults", "[config][corrupt]")
{
  const std::vector<uint8_t> truncated(Legacy::kDevelopConfig, Legacy::kDevelopConfig + sizeof(Legacy::kDevelopConfig) / 2);
  TEST_ASSERT_FALSE(InitFromLegacy(truncated));  // fell back
  AssertConfigEqual(Full(), Snapshot());

  // The defaults were written to NVS: the next boot loads them and leaves the bad file alone.
  Reload();
  AssertConfigEqual(Full(), Snapshot());
  TEST_ASSERT_EQUAL_INT(0, HostLfs::writeCount);
}

TEST_CASE("An oversized 1.x file is rejected, not aborted on", "[config][corrupt]")
{
  // A well-formed flatbuffer, just larger than the 1.x partition could ever hold: the Verifier's constructor
  // asserts (size < max_size), so the size is checked first.
  Full sample = MakeSampleConfig();
  sample.wifi.hostname.assign(5000, 'h');
  TEST_ASSERT_FALSE(InitFromLegacy(Serialize(sample)));
  AssertConfigEqual(Full(), Snapshot());
}

TEST_CASE("Random garbage is rejected", "[config][corrupt]")
{
  uint32_t state = 0x12345678;
  for (int round = 0; round < 300; ++round) {
    std::vector<uint8_t> data(4 + NextRandom(state) % 600);
    for (auto& b : data) b = static_cast<uint8_t>(NextRandom(state));
    Full parsed;
    char msg[48];
    snprintf(msg, sizeof(msg), "garbage round %d", round);
    TEST_ASSERT_FALSE_MESSAGE(Deserialize(data.data(), data.size(), parsed), msg);
  }
  TryImport(std::vector<uint8_t>(64, 0xFF));
}

TEST_CASE("Bit flips in a legacy config never crash the loader", "[config][corrupt]")
{
  // A flipped byte may still form a valid (different) config; what matters is that
  // verification keeps every read in bounds (run under ASan/UBSan to see it).
  for (std::size_t i = 0; i < sizeof(Legacy::kDevelopConfig); ++i) {
    for (uint8_t mask : {0x01, 0x80, 0xFF}) {
      std::vector<uint8_t> data(Legacy::kDevelopConfig, Legacy::kDevelopConfig + sizeof(Legacy::kDevelopConfig));
      data[i] ^= mask;
      Full parsed;
      Deserialize(data.data(), data.size(), parsed);
    }
  }
}

TEST_CASE("Out-of-range offsets in the root table are rejected", "[config][corrupt]")
{
  std::vector<uint8_t> data(Legacy::kDevelopConfig, Legacy::kDevelopConfig + sizeof(Legacy::kDevelopConfig));

  // Root offset pointing past the end of the buffer.
  data[0] = 0xF0;
  data[1] = 0xFF;
  TEST_ASSERT_FALSE(TryImport(data));

  // Root offset pointing at the last byte.
  const uint32_t last = static_cast<uint32_t>(data.size() - 1);
  data[0]             = static_cast<uint8_t>(last);
  data[1]             = static_cast<uint8_t>(last >> 8);
  TEST_ASSERT_FALSE(TryImport(data));
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
  TEST_ASSERT_FALSE(TryImport(data));

  // Correct length, but the terminating NUL overwritten.
  data[lenPos + 1]                 = 0x00;
  data[lenPos + 4 + needle.size()] = 'X';
  TEST_ASSERT_FALSE(TryImport(data));
}

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

TEST_CASE("Schema fields the firmware does not model are dropped on import", "[config][corrupt]")
{
  // estop.active / estop.latching (and wifi AP/LAN fields) exist in the 1.x schema but nothing in the firmware
  // reads them, so they are not imported and never stored. This pins that behaviour.
  namespace Fbs = Serialization::Configuration;
  flatbuffers::FlatBufferBuilder builder;
  auto estop = Fbs::CreateEStopConfig(builder, false, -1, /*active=*/true, /*latching=*/true);
  Fbs::FinishHubConfigBuffer(builder, Fbs::CreateHubConfig(builder, 0, 0, 0, 0, 0, 0, estop));
  TEST_ASSERT_TRUE(InitFromLegacy(builder.GetBufferPointer(), builder.GetSize()));

  flatbuffers::FlatBufferBuilder out;
  Fbs::FinishHubConfigBuffer(out, Config::GetAsFlatBuffer(out, true));
  const auto* root = Fbs::GetHubConfig(out.GetBufferPointer());
  TEST_ASSERT_NOT_NULL(root->estop());
  TEST_ASSERT_FALSE(root->estop()->active());
  TEST_ASSERT_FALSE(root->estop()->latching());
}

TEST_CASE("RemoveWiFiCredentials reports unknown IDs", "[config]")
{
  InitDefault();
  TEST_ASSERT_FALSE(Config::RemoveWiFiCredentials(42));
}

TEST_CASE("Out-of-range E-Stop GPIO in a 1.x config falls back to the default", "[config][corrupt]")
{
  // gpio_pin is an int8 in the file; values outside gpio_num_t must not be cast to it
  // (undefined behaviour, flagged by UBSan) and mean a corrupt section.
  namespace Fbs = Serialization::Configuration;
  for (int8_t pin : {static_cast<int8_t>(-128), static_cast<int8_t>(-2), static_cast<int8_t>(GPIO_NUM_MAX), static_cast<int8_t>(127)}) {
    flatbuffers::FlatBufferBuilder builder;
    auto estop = Fbs::CreateEStopConfig(builder, true, pin);
    auto root  = Fbs::CreateHubConfig(builder, 0, 0, 0, 0, 0, 0, estop);
    Fbs::FinishHubConfigBuffer(builder, root);

    char msg[32];
    snprintf(msg, sizeof(msg), "gpio_pin %d", pin);
    TEST_ASSERT_TRUE_MESSAGE(InitFromLegacy(builder.GetBufferPointer(), builder.GetSize()), msg);

    auto estopConfig = Config::GetEStopConfig();
    TEST_ASSERT_TRUE(estopConfig.has_value());
    TEST_ASSERT_EQUAL_INT_MESSAGE(Config::EStopConfig().gpioPin, estopConfig->gpioPin, msg);
    TEST_ASSERT_EQUAL_MESSAGE(Config::EStopConfig().enabled, estopConfig->enabled, msg);
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

  const Full loaded = Snapshot();
  TEST_ASSERT_EQUAL_INT(OPENSHOCK_RF_TX_GPIO, loaded.rf.txPin);
  TEST_ASSERT_EQUAL_size_t(1, loaded.wifi.credentialsList.size());
  TEST_ASSERT_EQUAL_STRING("ok", loaded.wifi.credentialsList[0].ssid.c_str());
  TEST_ASSERT_EQUAL_UINT16(30, loaded.otaUpdate.checkInterval);
}
