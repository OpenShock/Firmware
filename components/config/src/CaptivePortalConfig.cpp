#include "config/CaptivePortalConfig.h"

const char* const TAG = "Config::CaptivePortalConfig";

#include "config/internal/utils.h"
#include "Logging.h"

using namespace OpenShock::Config;

CaptivePortalConfig::CaptivePortalConfig()
  : alwaysEnabled(false)
{
}

CaptivePortalConfig::CaptivePortalConfig(bool alwaysEnabled)
  : alwaysEnabled(alwaysEnabled)
{
}

void CaptivePortalConfig::ToDefault()
{
  alwaysEnabled = false;
}

bool CaptivePortalConfig::FromFlatbuffers(const Serialization::Configuration::CaptivePortalConfig* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  alwaysEnabled = config->always_enabled();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::CaptivePortalConfig> CaptivePortalConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Serialization::Configuration::CreateCaptivePortalConfig(builder, alwaysEnabled);
}

bool CaptivePortalConfig::FromJSON(JSON::JsonView json)
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

  Internal::Utils::FromJsonBool(alwaysEnabled, json, "alwaysEnabled", false);

  return true;
}

void CaptivePortalConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "alwaysEnabled", alwaysEnabled);
  JSON::objEnd(gen, name);
}
