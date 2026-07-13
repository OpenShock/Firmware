#include "config/EStopConfig.h"

#include "Chipset.h"
#include "config/internal/utils.h"
#include "Logging.h"
#include "OpenShock.h"

const char* const TAG = "Config::EStopConfig";

using namespace OpenShock::Config;

EStopConfig::EStopConfig()
  : enabled(OpenShock::IsValidInputPin(CONFIG_OPENSHOCK_ESTOP_PIN))
  , gpioPin(static_cast<gpio_num_t>(CONFIG_OPENSHOCK_ESTOP_PIN))
{
}

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin)
  : enabled(enabled)
  , gpioPin(gpioPin)
{
}

void EStopConfig::ToDefault()
{
  enabled = OpenShock::IsValidInputPin(CONFIG_OPENSHOCK_ESTOP_PIN);
  gpioPin = static_cast<gpio_num_t>(CONFIG_OPENSHOCK_ESTOP_PIN);
}

bool EStopConfig::FromFlatbuffers(const Serialization::Configuration::EStopConfig* config)
{
  if (config == nullptr) {
    ToDefault();  // Set to default if config is null
    return true;
  }

  gpioPin = static_cast<gpio_num_t>(config->gpio_pin());

  if (OpenShock::IsValidInputPin(static_cast<int8_t>(gpioPin))) {
    enabled = config->enabled();
  } else {
    enabled = false;
  }

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::EStopConfig> EStopConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Serialization::Configuration::CreateEStopConfig(builder, enabled, gpioPin);
}

bool EStopConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    ToDefault();  // Set to default if config is null
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  Internal::Utils::FromJsonGpioNum(gpioPin, json, "gpioPin", static_cast<gpio_num_t>(CONFIG_OPENSHOCK_ESTOP_PIN));

  if (!Internal::Utils::FromJsonBool(enabled, json, "enabled", OpenShock::IsValidInputPin(gpioPin))) {
    OS_LOGE(TAG, "Failed to parse enabled");
    return false;
  }

  return true;
}

void EStopConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "enabled", enabled);
  json_gen_obj_set_int(gen, "gpioPin", gpioPin);
  JSON::objEnd(gen, name);
}
