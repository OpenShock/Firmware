#include "config/WiFiCredentials.h"

#include "config/internal/utils.h"
#include "Logging.h"
#include "util/HexUtils.h"

const char* const TAG = "Config::WiFiCredentials";

using namespace OpenShock::Config;

WiFiCredentials::WiFiCredentials() : id(0), ssid(), password() { }

WiFiCredentials::WiFiCredentials(std::uint8_t id, StringView ssid, StringView password) : id(id), ssid(ssid.toString()), password(password.toString()) { }

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

  if (ssid.empty()) {
    ESP_LOGE(TAG, "ssid is empty");
    return false;
  }

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

  Internal::Utils::FromJsonU8(id, json, "id", 0);
  Internal::Utils::FromJsonStr(ssid, json, "ssid", "");
  Internal::Utils::FromJsonStr(password, json, "password", "");

  if (ssid.empty()) {
    ESP_LOGE(TAG, "ssid is empty");
    return false;
  }

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
