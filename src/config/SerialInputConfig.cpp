#include "config/SerialInputConfig.h"

const char* const TAG = "Config::SerialInputConfig";

#include "config/internal/utils.h"
#include "Logging.h"

using namespace OpenShock::Config;

SerialInputConfig::SerialInputConfig()
  : echoEnabled(true)
{
}

SerialInputConfig::SerialInputConfig(bool echoEnabled)
  : echoEnabled(echoEnabled)
{
}

void SerialInputConfig::ToDefault() {
  echoEnabled = true;
}

bool SerialInputConfig::FromFlatbuffers(const Serialization::Configuration::SerialInputConfig* config) {
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  echoEnabled = config->echo_enabled();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::SerialInputConfig> SerialInputConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  return Serialization::Configuration::CreateSerialInputConfig(builder, echoEnabled);
}

bool SerialInputConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  if (cJSON_IsObject(json) == 0) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  Internal::Utils::FromJsonBool(echoEnabled, json, "echoEnabled", true);

  return true;
}

cJSON* SerialInputConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "echoEnabled", echoEnabled);

  return root;
}
