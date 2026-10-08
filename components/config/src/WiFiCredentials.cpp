#include "config/WiFiCredentials.h"

const char* const TAG = "Config::WiFiCredentials";

#include "config/internal/utils.h"
#include "config/WiFiAuthMode.h"
#include "Logging.h"

#include "util/HexUtils.h"

#include <cstring>

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

bool WiFiCredentials::FromFlatbuffers(const Serialization::Configuration::WiFiCredentials* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  id = config->id();
  Internal::Utils::FromFbsStr(ssid, config->ssid(), "");
  Internal::Utils::FromFbsStr(password, config->password(), "");
  authMode = FromFbsAuthMode(config->auth_mode());

  auto fbsBssid = config->bssid();
  if (fbsBssid != nullptr) {
    memcpy(bssid.data(), fbsBssid->bytes()->data(), 6);
  } else {
    bssid.fill(0);
  }

  if (ssid.empty()) {
    OS_LOGE(TAG, "ssid is empty");
    return false;
  }

  return true;
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

  if (!json["id"].tryGetU8(id)) id = 0;

  if (!json["ssid"].tryGetStr(ssid)) ssid.clear();

  if (!json["password"].tryGetStr(password)) password.clear();

  uint8_t authModeVal = static_cast<uint8_t>(FbsAuthMode::UNKNOWN);
  if (!json["authMode"].tryGetU8(authModeVal)) authModeVal = static_cast<uint8_t>(FbsAuthMode::UNKNOWN);
  authMode = FromFbsAuthMode(static_cast<FbsAuthMode>(authModeVal));

  bssid.fill(0);
  std::string bssidStr;
  if (json["bssid"].tryGetStr(bssidStr) && bssidStr.size() == 12) {
    // Parse into a temporary: TryParseHex writes byte by byte and stops at the first bad pair, and a partial
    // BSSID would pin the network to an AP that doesn't exist.
    std::array<uint8_t, 6> parsed {};
    if (HexUtils::TryParseHex(bssidStr.data(), bssidStr.size(), parsed.data(), parsed.size()) == parsed.size()) {
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
  json_gen_obj_set_int(gen, "authMode", static_cast<uint8_t>(ToFbsAuthMode(authMode)));
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
