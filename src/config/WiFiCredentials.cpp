#include "config/WiFiCredentials.h"

#include "config/internal/utils.h"
#include "Logging.h"
#include "util/HexUtils.h"

const char* const TAG = "Config::WiFiCredentials";

using namespace OpenShock::Config;

WiFiCredentials::WiFiCredentials() : id(0), ssid(), password() { }

WiFiCredentials::WiFiCredentials(std::uint8_t id, const std::string& ssid, const std::string& password) : id(id), ssid(ssid), password(password) { }

void WiFiCredentials::ToDefault() {
  id = 0;
  ssid.clear();
  password.clear();
}

bool WiFiCredentials::FromFlatbuffers(const Serialization::Configuration::WiFiCredentials* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  id = config->id();
  Internal::Utils::FromFbsStr(ssid, config->ssid(), "");
  Internal::Utils::FromFbsStr(password, config->password(), "");

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiCredentials> WiFiCredentials::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  auto ssidOffset = builder.CreateString(ssid);

  flatbuffers::Offset<flatbuffers::String> passwordOffset;
  if (withSensitiveData) {
    passwordOffset = builder.CreateString(password);
  } else {
    passwordOffset = 0;
  }

  return Serialization::Configuration::CreateWiFiCredentials(builder, id, ssidOffset, passwordOffset);
}

bool WiFiCredentials::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  const cJSON* idJson = cJSON_GetObjectItemCaseSensitive(json, "id");
  if (idJson == nullptr) {
    ESP_LOGV(TAG, "id was null");
    id = 0;
  } else {
    if (cJSON_IsNumber(idJson) == 0) {
      ESP_LOGE(TAG, "id is not a number");
      return false;
    }

    if (idJson->valueint < 0 || idJson->valueint > UINT8_MAX) {
      ESP_LOGE(TAG, "id is out of range");
      return false;
    }

    id = idJson->valueint;
  }

  const cJSON* ssidJson = cJSON_GetObjectItemCaseSensitive(json, "ssid");
  if (ssidJson == nullptr) {
    ESP_LOGE(TAG, "ssid is null");
    return false;
  }

  if (cJSON_IsString(ssidJson) == 0) {
    ESP_LOGE(TAG, "ssid is not a string");
    return false;
  }

  ssid = ssidJson->valuestring;

  const cJSON* passwordJson = cJSON_GetObjectItemCaseSensitive(json, "password");
  if (passwordJson == nullptr) {
    ESP_LOGE(TAG, "password is null");
    return false;
  }

  if (cJSON_IsString(passwordJson) == 0) {
    ESP_LOGE(TAG, "password is not a string");
    return false;
  }

  password = passwordJson->valuestring;

  return true;
}

cJSON* WiFiCredentials::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddNumberToObject(root, "id", id);  //-V2564
  cJSON_AddStringToObject(root, "ssid", ssid.c_str());
  if (withSensitiveData) {
    cJSON_AddStringToObject(root, "password", password.c_str());
  }

  return root;
}
