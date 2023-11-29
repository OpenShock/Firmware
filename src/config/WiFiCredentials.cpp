#include "config/WiFiCredentials.h"

#include "config/internal/utils.h"
#include "Logging.h"
#include "util/HexUtils.h"

const char* const TAG = "Config::WiFiCredentials";

using namespace OpenShock::Config;

WiFiCredentials::WiFiCredentials() {
  ToDefault();
}

WiFiCredentials::WiFiCredentials(std::uint8_t id, const std::string& ssid, const std::uint8_t (&bssid)[6], const std::string& password) {
  this->id       = id;
  this->ssid     = ssid;
  this->password = password;
  std::copy(std::begin(bssid), std::end(bssid), this->bssid);
}

void WiFiCredentials::ToDefault() {
  id   = 0;
  ssid = "";
  std::fill(std::begin(bssid), std::end(bssid), 0);
  password = "";
}

bool WiFiCredentials::FromFlatbuffers(const Serialization::Configuration::WiFiCredentials* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  id = config->id();
  Internal::Utils::FromFbsStr(ssid, config->ssid(), "");
  Internal::Utils::FromFbsStr(password, config->password(), "");

  auto fbsBssid = config->bssid();
  if (fbsBssid != nullptr) {
    auto fbsBssidArr = fbsBssid->array();
    if (fbsBssidArr != nullptr) {
      std::copy(fbsBssidArr->begin(), fbsBssidArr->end(), bssid);
    } else {
      std::fill(std::begin(bssid), std::end(bssid), 0);
    }
  } else {
    std::fill(std::begin(bssid), std::end(bssid), 0);
  }

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiCredentials> WiFiCredentials::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const {
  Serialization::Configuration::BSSID fbsBssid(bssid);

  return Serialization::Configuration::CreateWiFiCredentials(builder, id, builder.CreateString(ssid), &fbsBssid, builder.CreateString(password));
}

bool WiFiCredentials::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (!cJSON_IsObject(json)) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  const cJSON* idJson = cJSON_GetObjectItemCaseSensitive(json, "id");
  if (idJson == nullptr) {
    ESP_LOGE(TAG, "id is null");
    return false;
  }

  if (!cJSON_IsNumber(idJson)) {
    ESP_LOGE(TAG, "id is not a number");
    return false;
  }

  if (idJson->valueint < 0 || idJson->valueint > UINT8_MAX) {
    ESP_LOGE(TAG, "id is out of range");
    return false;
  }

  id = idJson->valueint;

  const cJSON* ssidJson = cJSON_GetObjectItemCaseSensitive(json, "ssid");
  if (ssidJson == nullptr) {
    ESP_LOGE(TAG, "ssid is null");
    return false;
  }

  if (!cJSON_IsString(ssidJson)) {
    ESP_LOGE(TAG, "ssid is not a string");
    return false;
  }

  ssid = ssidJson->valuestring;

  const cJSON* bssidJson = cJSON_GetObjectItemCaseSensitive(json, "bssid");
  if (bssidJson == nullptr) {
    ESP_LOGE(TAG, "bssid is null");
    return false;
  }

  if (!cJSON_IsString(bssidJson)) {
    ESP_LOGE(TAG, "bssid is not a string");
    return false;
  }

  std::size_t bssidLen = strlen(bssidJson->valuestring);

  if (bssidLen != 17) {
    ESP_LOGE(TAG, "bssid is not a valid MAC address");
    return false;
  }

  if (!HexUtils::TryParseHexMac(bssidJson->valuestring, bssidLen, bssid, 6)) {
    ESP_LOGE(TAG, "bssid has a invalid format");
    return false;
  }

  return true;
}

cJSON* WiFiCredentials::ToJSON() const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "id", id);
  cJSON_AddStringToObject(root, "ssid", ssid.c_str());

  char bssidStr[17];
  HexUtils::ToHexMac<6>(bssid, bssidStr);
  cJSON_AddStringToObject(root, "bssid", bssidStr);

  return root;
}
