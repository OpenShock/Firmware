#include "config/SerialInputConfig.h"

#include "Logging.h"

const char* const TAG = "Config::SerialInputConfig";

using namespace OpenShock::Config;

SerialInputConfig::SerialInputConfig() : echoEnabled(true) { }

SerialInputConfig::SerialInputConfig(bool echoEnabled) {
  this->echoEnabled = echoEnabled;
}

void SerialInputConfig::ToDefault() {
  echoEnabled = true;
}

bool SerialInputConfig::FromFlatbuffers(const Serialization::Configuration::SerialInputConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  echoEnabled = config->echo_enabled();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::SerialInputConfig> SerialInputConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  return Serialization::Configuration::CreateSerialInputConfig(builder, echoEnabled);
}

bool SerialInputConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  const cJSON* echoEnabledJson = cJSON_GetObjectItemCaseSensitive(json, "echoEnabled");
  if (echoEnabledJson == nullptr) {
    ESP_LOGE(TAG, "echoEnabled is null");
    return false;
  }

  if (cJSON_IsBool(echoEnabledJson) == 0) {
    ESP_LOGE(TAG, "echoEnabled is not a bool");
    return false;
  }

  echoEnabled = cJSON_IsTrue(echoEnabledJson);

  return true;
}

cJSON* SerialInputConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "echoEnabled", echoEnabled);

  return root;
}
