#include "config/BackendConfig.h"

#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::BackendConfig";

using namespace OpenShock::Config;

BackendConfig::BackendConfig() : domain(OPENSHOCK_API_DOMAIN), authToken(), lcgOverride() { }

BackendConfig::BackendConfig(StringView domain, StringView authToken, StringView lcgOverride) : domain(domain.toString()), authToken(authToken.toString()), lcgOverride(lcgOverride.toString()) { }

void BackendConfig::ToDefault() {
  domain = OPENSHOCK_API_DOMAIN;
  authToken.clear();
}

bool BackendConfig::FromFlatbuffers(const Serialization::Configuration::BackendConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  Internal::Utils::FromFbsStr(domain, config->domain(), OPENSHOCK_API_DOMAIN);
  Internal::Utils::FromFbsStr(authToken, config->auth_token(), "");
  Internal::Utils::FromFbsStr(lcgOverride, config->lcg_override(), "");

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::BackendConfig> BackendConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  auto domainOffset = builder.CreateString(domain);

  flatbuffers::Offset<flatbuffers::String> authTokenOffset;
  if (withSensitiveData) {
    authTokenOffset = builder.CreateString(authToken);
  } else {
    authTokenOffset = 0;
  }

  auto lcgOverrideOffset = builder.CreateString(lcgOverride);

  return Serialization::Configuration::CreateBackendConfig(builder, domainOffset, authTokenOffset, lcgOverrideOffset);
}

bool BackendConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  Internal::Utils::FromJsonStr(domain, json, "domain", OPENSHOCK_API_DOMAIN);
  Internal::Utils::FromJsonStr(authToken, json, "authToken", "");
  Internal::Utils::FromJsonStr(lcgOverride, json, "lcgOverride", "");

  return true;
}

cJSON* BackendConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddStringToObject(root, "domain", domain.c_str());

  if (withSensitiveData) {
    cJSON_AddStringToObject(root, "authToken", authToken.c_str());
  }

  cJSON_AddStringToObject(root, "lcgOverride", lcgOverride.c_str());

  return root;
}
