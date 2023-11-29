#include "config/BackendConfig.h"

#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::BackendConfig";

using namespace OpenShock::Config;

void BackendConfig::ToDefault() {
  authToken = "";
}

bool BackendConfig::FromFlatbuffers(const Serialization::Configuration::BackendConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  Internal::Utils::FromFbsStr(authToken, config->auth_token(), "");

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::BackendConfig> BackendConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const {
  return Serialization::Configuration::CreateBackendConfig(builder, builder.CreateString(authToken));
}

bool BackendConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (!cJSON_IsObject(json)) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  const cJSON* authTokenJson = cJSON_GetObjectItemCaseSensitive(json, "authToken");
  if (authTokenJson == nullptr) {
    ESP_LOGE(TAG, "authToken is null");
    return false;
  }

  if (!cJSON_IsString(authTokenJson)) {
    ESP_LOGE(TAG, "authToken is not a string");
    return false;
  }

  authToken = authTokenJson->valuestring;

  return true;
}

cJSON* BackendConfig::ToJSON() const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddStringToObject(root, "authToken", authToken.c_str());

  return root;
}
