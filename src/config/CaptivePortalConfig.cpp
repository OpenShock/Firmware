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

bool CaptivePortalConfig::FromJSON(const cJSON* json)
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

  Internal::Utils::FromJsonBool(alwaysEnabled, json, "alwaysEnabled", false);

  return true;
}

cJSON* CaptivePortalConfig::ToJSON(bool withSensitiveData) const
{
  cJSON* root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "alwaysEnabled", alwaysEnabled);

  return root;
}
