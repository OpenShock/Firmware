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

bool LanConfig::FromJSON(JSON::JsonView json)
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

  if (!json["apiKeyEnabled"].tryGetBool(apiKeyEnabled)) apiKeyEnabled = false;

  if (!json["apiKey"].tryGetStr(apiKey)) apiKey.clear();

  return true;
}

void LanConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "apiKeyEnabled", apiKeyEnabled);

  if (withSensitiveData) {
    JSON::objSetString(gen, "apiKey", apiKey);
  }
  JSON::objEnd(gen, name);
}
