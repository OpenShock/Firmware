// Generator for main/legacy_fixtures.h. NOT part of the host test build.
//
// Serializes a hub config exactly the way the Arduino firmware did: the bodies of its
// RootConfig / *Config::ToFlatbuffers (same Create* calls in the same order), then
// FinishHubConfigBuffer as in its Config.cpp trySaveConfig. Build it against the old
// firmware's generated schema header and pinned flatbuffers, e.g. from the firmware
// repo root (any host g++):
//
//   mkdir -p /tmp/gen/v152/serialization/_fbs /tmp/gen/dev/serialization/_fbs /tmp/gen/fb152 /tmp/gen/fbdev
//   git show 1.5.2:include/serialization/_fbs/HubConfig_generated.h > /tmp/gen/v152/serialization/_fbs/HubConfig_generated.h
//   git show develop:include/serialization/_fbs/HubConfig_generated.h > /tmp/gen/dev/serialization/_fbs/HubConfig_generated.h
//   git show develop:include/serialization/_fbs/WifiAuthMode_generated.h > /tmp/gen/dev/serialization/_fbs/WifiAuthMode_generated.h
//   git -C components/flatbuffers/flatbuffers archive 8b02fe6178427b96aea25396b53ea4ae8cadd7d8 include | tar -x -C /tmp/gen/fb152
//   git -C components/flatbuffers/flatbuffers archive 81edeb17d9118143f2c81caf27edfb0df401279e include | tar -x -C /tmp/gen/fbdev
//   g++ -std=c++17 -I/tmp/gen/v152 -I/tmp/gen/fb152/include gen_legacy_config.cpp -o /tmp/gen/v152.out && /tmp/gen/v152.out
//   g++ -std=c++17 -DLEGACY_DEVELOP -I/tmp/gen/dev -I/tmp/gen/dev/serialization/_fbs -I/tmp/gen/fbdev/include gen_legacy_config.cpp -o /tmp/gen/dev.out && /tmp/gen/dev.out
//
// The flatbuffers commits are the ones each tag's platformio.ini pinned.
#include "serialization/_fbs/HubConfig_generated.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace OpenShock::Serialization;
namespace C = OpenShock::Serialization::Configuration;

struct Creds {
  uint8_t id;
  std::string ssid, password;
#ifdef LEGACY_DEVELOP
  Types::WifiAuthMode authMode;
  bool pinned;
  uint8_t bssid[6];
#endif
};

static void emit(const char* name, bool keepAlive)
{
  flatbuffers::FlatBufferBuilder builder;
  const bool withSensitiveData = true;

  // RootConfig::ToFlatbuffers order: rf, wifi, captivePortal, backend, serialInput, otaUpdate, estop
  auto rfOffset = C::CreateRFConfig(builder, 15, keepAlive);

  std::vector<Creds> credentialsList = {
#ifdef LEGACY_DEVELOP
    {1, "HomeNet", "hunter22", Types::WifiAuthMode::WPA2_PSK,  true, {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc}},
    {2,   "Guest",         "",     Types::WifiAuthMode::Open, false,                                   {}},
    {3,  "Office",  "s3cret!",  Types::WifiAuthMode::UNKNOWN, false,                                   {}},
#else
    {1, "HomeNet", "hunter22"},
    {2, "Guest", ""},
    {3, "Office", "s3cret!"},
#endif
  };
  std::vector<flatbuffers::Offset<C::WiFiCredentials>> fbsCredentialsList;
  fbsCredentialsList.reserve(credentialsList.size());
  for (auto& c : credentialsList) {
    auto ssidOffset = builder.CreateString(c.ssid);
    flatbuffers::Offset<flatbuffers::String> passwordOffset;
    if (withSensitiveData) {
      passwordOffset = builder.CreateString(c.password);
    } else {
      passwordOffset = 0;
    }
#ifdef LEGACY_DEVELOP
    const C::MacAddress* bssidPtr = nullptr;
    C::MacAddress bssidStruct;
    if (c.pinned) {
      bssidStruct = C::MacAddress(flatbuffers::span<const uint8_t, 6>(c.bssid, 6));
      bssidPtr    = &bssidStruct;
    }
    fbsCredentialsList.push_back(C::CreateWiFiCredentials(builder, c.id, ssidOffset, passwordOffset, c.authMode, bssidPtr));
#else
    fbsCredentialsList.push_back(C::CreateWiFiCredentials(builder, c.id, ssidOffset, passwordOffset));
#endif
  }
  std::string accessPointSSID = "MyHubAP", hostname = "my-hub";
  auto wifiOffset = C::CreateWiFiConfig(builder, builder.CreateString(accessPointSSID), builder.CreateString(hostname), builder.CreateVector(fbsCredentialsList));

  auto captivePortalOffset = C::CreateCaptivePortalConfig(builder, true);

  auto domainOffset    = builder.CreateString(std::string("api.example.org"));
  auto authTokenOffset = builder.CreateString(std::string("tok_0123456789abcdef"));
  auto backendOffset   = C::CreateBackendConfig(builder, domainOffset, authTokenOffset);

  auto serialInputOffset = C::CreateSerialInputConfig(builder, false);

  auto otaUpdateOffset = C::CreateOtaUpdateConfig(builder, false, builder.CreateString(std::string("fw.example.org")), C::OtaUpdateChannel::Beta, true, true, 120, false, true, 4242, C::OtaUpdateStep::Validating);

  auto estopOffset = C::CreateEStopConfig(builder, true, 13);

  auto root = C::CreateHubConfig(builder, rfOffset, wifiOffset, captivePortalOffset, backendOffset, serialInputOffset, otaUpdateOffset, estopOffset);
  C::FinishHubConfigBuffer(builder, root);

  const uint8_t* p = builder.GetBufferPointer();
  std::size_t n    = builder.GetSize();
  printf("  // %s (%zu bytes)\n  static const uint8_t %s[] = {\n", name, n, name);
  for (std::size_t i = 0; i < n; i += 16) {
    printf("   ");
    for (std::size_t j = i; j < n && j < i + 16; ++j) printf(" 0x%02x,", p[j]);
    printf("\n");
  }
  printf("  };\n");
}

int main()
{
#ifdef LEGACY_DEVELOP
  emit("kDevelopConfig", false);
#else
  emit("kV152Config", true);
  emit("kV152ConfigKeepAliveOff", false);
#endif
}
