#include "config/LanConfig.h"

const char* const TAG = "Config::LanConfig";

#include "config/internal/utils.h"
#include "Logging.h"

using namespace OpenShock::Config;

LanConfig::LanConfig()
  : apiKeyEnabled(false)
  , apiKey()
{
}

LanConfig::LanConfig(bool apiKeyEnabled, std::string_view apiKey)
  : apiKeyEnabled(apiKeyEnabled)
  , apiKey(apiKey)
{
}

void LanConfig::ToDefault()
{
  apiKeyEnabled = false;
  apiKey.clear();
}

bool LanConfig::FromFlatbuffers(const Serialization::Configuration::LanConfig* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  apiKeyEnabled = config->api_key_enabled();
  Internal::Utils::FromFbsStr(apiKey, config->api_key(), "");

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::LanConfig> LanConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  flatbuffers::Offset<flatbuffers::String> apiKeyOffset;
  if (withSensitiveData) {
    apiKeyOffset = builder.CreateString(apiKey);
  } else {
    apiKeyOffset = 0;
  }

  return Serialization::Configuration::CreateLanConfig(builder, apiKeyEnabled, apiKeyOffset);
}

bool LanConfig::FromJSON(const cJSON* json)
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

  Internal::Utils::FromJsonBool(apiKeyEnabled, json, "apiKeyEnabled", false);
  Internal::Utils::FromJsonStr(apiKey, json, "apiKey", "");

  return true;
}

cJSON* LanConfig::ToJSON(bool withSensitiveData) const
{
  cJSON* root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "apiKeyEnabled", apiKeyEnabled);

  if (withSensitiveData) {
    cJSON_AddStringToObject(root, "apiKey", apiKey.c_str());
  }

  return root;
}
