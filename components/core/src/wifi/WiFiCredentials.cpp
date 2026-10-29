#include "wifi/WiFiCredentials.h"

const char* const TAG = "Config::WiFiCredentials";

#include "Logging.h"
#include "wifi/WiFiAuthMode.h"

#include "util/HexUtils.h"
#include "json/Json.h"

#include <algorithm>
#include <cstring>
#include <utility>

using namespace OpenShock::Config;
using FbsAuthMode = OpenShock::Serialization::Types::WifiAuthMode;

WiFiCredentials::WiFiCredentials()
  : id(0)
  , ssid()
  , password()
  , authMode(WIFI_AUTH_MAX)
  , bssid({0})
{
}

WiFiCredentials::WiFiCredentials(uint8_t id, std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode)
  : id(id)
  , ssid(ssid)
  , password(password)
  , authMode(authMode)
  , bssid({0})
{
}

bool WiFiCredentials::HasPinnedBSSID() const
{
  for (auto b : bssid) {
    if (b != 0) return true;
  }
  return false;
}

void WiFiCredentials::ToDefault()
{
  id = 0;
  ssid.clear();
  password.clear();
  authMode = WIFI_AUTH_MAX;
  bssid.fill(0);
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiCredentials> WiFiCredentials::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  auto ssidOffset = builder.CreateString(ssid);

  flatbuffers::Offset<flatbuffers::String> passwordOffset;
  if (withSensitiveData) {
    passwordOffset = builder.CreateString(password);
  } else {
    passwordOffset = 0;
  }

  const Serialization::Configuration::MacAddress* bssidPtr = nullptr;
  Serialization::Configuration::MacAddress bssidStruct;
  if (HasPinnedBSSID()) {
    bssidStruct = Serialization::Configuration::MacAddress(flatbuffers::span<const uint8_t, 6>(bssid.data(), 6));
    bssidPtr    = &bssidStruct;
  }

  return Serialization::Configuration::CreateWiFiCredentials(builder, id, ssidOffset, passwordOffset, ToFbsAuthMode(authMode), bssidPtr);
}

bool WiFiCredentials::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  id = json["id"].tryGetU8().value_or(0);

  ssid = json["ssid"].tryGetStr().value_or(std::string());

  password = json["password"].tryGetStr().value_or(std::string());

  uint8_t authModeVal = json["authMode"].tryGetU8().value_or(std::to_underlying(FbsAuthMode::UNKNOWN));
  authMode            = FromFbsAuthMode(static_cast<FbsAuthMode>(authModeVal));

  bssid.fill(0);
  if (auto bssidStr = json["bssid"].tryGetStr(); bssidStr && bssidStr->size() == 12) {
    // Parse into a temporary: TryParseHex writes byte by byte and stops at the first bad pair, and a partial
    // BSSID would pin the network to an AP that doesn't exist.
    std::array<uint8_t, 6> parsed {};
    if (HexUtils::TryParseHex(*bssidStr, parsed) == parsed.size()) {
      bssid = parsed;
    }
  }

  if (ssid.empty()) {
    OS_LOGE(TAG, "ssid is empty");
    return false;
  }

  return true;
}

void WiFiCredentials::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_int(gen, "id", id);
  JSON::objSetString(gen, "ssid", ssid);
  if (withSensitiveData) {
    JSON::objSetString(gen, "password", password);
  }
  json_gen_obj_set_int(gen, "authMode", std::to_underlying(ToFbsAuthMode(authMode)));
  if (HasPinnedBSSID()) {
    char hex[13];
    for (std::size_t i = 0; i < bssid.size(); ++i) {
      HexUtils::ToHex(bssid[i], &hex[i * 2]);
    }
    hex[12] = '\0';
    JSON::objSetString(gen, "bssid", hex);
  }
  JSON::objEnd(gen, name);
}

// Blob layout, version 1: version u8, id u8, auth mode u8 (kBlobAuthModes), bssid u8[6] (all zero: no pin), ssid
// length u8, ssid, password length u8, password.
//
// Versioning: a new field is appended and the version stays 1, since a reader ignores whatever follows the fields it
// knows; firmware rolled back to an older build then still reads every credential. The version changes only for a
// change an older reader must not misread, and that costs a rolled-back hub its saved networks.
static constexpr uint8_t kBlobVersion = 1;

// The auth mode codes the blob stores. Owned by this format alone: neither ESP-IDF's wifi_auth_mode_t nor the
// FlatBuffers schema may renumber what is on flash. Never reuse or renumber a code; append new ones.
static constexpr std::pair<uint8_t, wifi_auth_mode_t> kBlobAuthModes[] = {
  {0,            WIFI_AUTH_OPEN},
  {1,             WIFI_AUTH_WEP},
  {2,         WIFI_AUTH_WPA_PSK},
  {3,        WIFI_AUTH_WPA2_PSK},
  {4,    WIFI_AUTH_WPA_WPA2_PSK},
  {5, WIFI_AUTH_WPA2_ENTERPRISE},
  {6,        WIFI_AUTH_WPA3_PSK},
  {7,   WIFI_AUTH_WPA2_WPA3_PSK},
  {8,        WIFI_AUTH_WAPI_PSK},
};
static constexpr uint8_t kBlobAuthUnknown = 9;

static uint8_t toBlobAuthMode(wifi_auth_mode_t mode)
{
  for (const auto& [code, known] : kBlobAuthModes) {
    if (known == mode) {
      return code;
    }
  }
  return kBlobAuthUnknown;
}

static wifi_auth_mode_t fromBlobAuthMode(uint8_t code)
{
  for (const auto& [known, mode] : kBlobAuthModes) {
    if (known == code) {
      return mode;
    }
  }
  return WIFI_AUTH_MAX;
}

std::vector<uint8_t> WiFiCredentials::ToBlob() const
{
  std::vector<uint8_t> blob;
  blob.reserve(11 + ssid.size() + password.size());
  blob.push_back(kBlobVersion);
  blob.push_back(id);
  blob.push_back(toBlobAuthMode(authMode));
  blob.insert(blob.end(), bssid.begin(), bssid.end());
  blob.push_back(static_cast<uint8_t>(std::min<std::size_t>(ssid.size(), 255)));
  blob.insert(blob.end(), ssid.begin(), ssid.begin() + std::min<std::size_t>(ssid.size(), 255));
  blob.push_back(static_cast<uint8_t>(std::min<std::size_t>(password.size(), 255)));
  blob.insert(blob.end(), password.begin(), password.begin() + std::min<std::size_t>(password.size(), 255));
  return blob;
}

bool WiFiCredentials::FromBlob(std::span<const uint8_t> blob)
{
  if (blob.size() < 11 || blob[0] != kBlobVersion) {
    OS_LOGE(TAG, "Unknown credential blob (%zu bytes, version %u)", blob.size(), blob.empty() ? 0u : blob[0]);
    return false;
  }
  std::size_t pos    = 1;
  uint8_t storedId   = blob[pos++];
  uint8_t storedAuth = blob[pos++];
  std::array<uint8_t, 6> storedBssid;
  std::copy_n(blob.begin() + pos, 6, storedBssid.begin());
  pos += 6;
  const std::size_t ssidLen = blob[pos++];
  if (blob.size() < pos + ssidLen + 1) {
    OS_LOGE(TAG, "Credential blob truncated");
    return false;
  }
  std::string storedSsid(reinterpret_cast<const char*>(blob.data() + pos), ssidLen);
  pos += ssidLen;
  const std::size_t pwLen = blob[pos++];
  if (blob.size() < pos + pwLen) {
    OS_LOGE(TAG, "Credential blob truncated");
    return false;
  }
  std::string storedPassword(reinterpret_cast<const char*>(blob.data() + pos), pwLen);
  if (storedId == 0 || storedSsid.empty()) {
    OS_LOGE(TAG, "Credential blob has no id or no ssid");
    return false;
  }

  id       = storedId;
  ssid     = std::move(storedSsid);
  password = std::move(storedPassword);
  authMode = fromBlobAuthMode(storedAuth);
  bssid    = storedBssid;
  return true;
}
