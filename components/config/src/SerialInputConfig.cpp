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

void SerialInputConfig::ToDefault()
{
  echoEnabled = true;
}

bool SerialInputConfig::FromFlatbuffers(const Serialization::Configuration::SerialInputConfig* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  echoEnabled = config->echo_enabled();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::SerialInputConfig> SerialInputConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Serialization::Configuration::CreateSerialInputConfig(builder, echoEnabled);
}

bool SerialInputConfig::FromJSON(JSON::JsonView json)
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

  if (!json["echoEnabled"].tryGetBool(echoEnabled)) echoEnabled = true;

  return true;
}

void SerialInputConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "echoEnabled", echoEnabled);
  JSON::objEnd(gen, name);
}
