#include "config/WiFiCredentials.h"

const char* const TAG = "Config::WiFiCredentials";

#include "config/internal/utils.h"
#include "Logging.h"
#include "util/HexUtils.h"

using namespace OpenShock::Config;
using FbsAuthMode = OpenShock::Serialization::Types::WifiAuthMode;

static FbsAuthMode toFbsAuthMode(wifi_auth_mode_t mode)
{
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return FbsAuthMode::Open;
    case WIFI_AUTH_WEP:
      return FbsAuthMode::WEP;
    case WIFI_AUTH_WPA_PSK:
      return FbsAuthMode::WPA_PSK;
    case WIFI_AUTH_WPA2_PSK:
      return FbsAuthMode::WPA2_PSK;
    case WIFI_AUTH_WPA_WPA2_PSK:
      return FbsAuthMode::WPA_WPA2_PSK;
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return FbsAuthMode::WPA2_ENTERPRISE;
    case WIFI_AUTH_WPA3_PSK:
      return FbsAuthMode::WPA3_PSK;
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return FbsAuthMode::WPA2_WPA3_PSK;
    case WIFI_AUTH_WAPI_PSK:
      return FbsAuthMode::WAPI_PSK;
    default:
      return FbsAuthMode::UNKNOWN;
  }
}

static wifi_auth_mode_t fromFbsAuthMode(FbsAuthMode mode)
{
  switch (mode) {
    case FbsAuthMode::Open:
      return WIFI_AUTH_OPEN;
    case FbsAuthMode::WEP:
      return WIFI_AUTH_WEP;
    case FbsAuthMode::WPA_PSK:
      return WIFI_AUTH_WPA_PSK;
    case FbsAuthMode::WPA2_PSK:
      return WIFI_AUTH_WPA2_PSK;
    case FbsAuthMode::WPA_WPA2_PSK:
      return WIFI_AUTH_WPA_WPA2_PSK;
    case FbsAuthMode::WPA2_ENTERPRISE:
      return WIFI_AUTH_WPA2_ENTERPRISE;
    case FbsAuthMode::WPA3_PSK:
      return WIFI_AUTH_WPA3_PSK;
    case FbsAuthMode::WPA2_WPA3_PSK:
      return WIFI_AUTH_WPA2_WPA3_PSK;
    case FbsAuthMode::WAPI_PSK:
      return WIFI_AUTH_WAPI_PSK;
    default:
      return WIFI_AUTH_MAX;
  }
}

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
  authMode = fromFbsAuthMode(config->auth_mode());

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

  return Serialization::Configuration::CreateWiFiCredentials(builder, id, ssidOffset, passwordOffset, toFbsAuthMode(authMode), bssidPtr);
}

bool WiFiCredentials::FromJSON(const cJSON* json)
{
  if (json == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  if (cJSON_IsObject(json) == 0) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  Internal::Utils::FromJsonU8(id, json, "id", 0);
  Internal::Utils::FromJsonStr(ssid, json, "ssid", "");
  Internal::Utils::FromJsonStr(password, json, "password", "");

  uint8_t authModeVal = static_cast<uint8_t>(FbsAuthMode::UNKNOWN);
  Internal::Utils::FromJsonU8(authModeVal, json, "authMode", static_cast<uint8_t>(FbsAuthMode::UNKNOWN));
  authMode = fromFbsAuthMode(static_cast<FbsAuthMode>(authModeVal));

  bssid.fill(0);
  const cJSON* bssidJson = cJSON_GetObjectItemCaseSensitive(json, "bssid");
  if (cJSON_IsString(bssidJson) && bssidJson->valuestring != nullptr && strlen(bssidJson->valuestring) == 12) {
    HexUtils::TryParseHex(bssidJson->valuestring, 12, bssid.data(), bssid.size());
  }

  if (ssid.empty()) {
    OS_LOGE(TAG, "ssid is empty");
    return false;
  }

  return true;
}

cJSON* WiFiCredentials::ToJSON(bool withSensitiveData) const
{
  cJSON* root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "id", id);  //-V2564
  cJSON_AddStringToObject(root, "ssid", ssid.c_str());
  if (withSensitiveData) {
    cJSON_AddStringToObject(root, "password", password.c_str());
  }
  cJSON_AddNumberToObject(root, "authMode", static_cast<uint8_t>(toFbsAuthMode(authMode)));
  if (HasPinnedBSSID()) {
    char hex[13];
    for (std::size_t i = 0; i < bssid.size(); ++i) {
      HexUtils::ToHex(bssid[i], &hex[i * 2]);
    }
    hex[12] = '\0';
    cJSON_AddStringToObject(root, "bssid", hex);
  }

  return root;
}
