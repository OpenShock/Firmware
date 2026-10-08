#include "config/EStopConfig.h"

#include "Chipset.h"
#include "config/internal/utils.h"
#include "Logging.h"
#include "OpenShock.h"

const char* const TAG = "Config::EStopConfig";

using namespace OpenShock::Config;

EStopConfig::EStopConfig()
  : enabled(OpenShock::IsValidInputPin(OPENSHOCK_ESTOP_PIN))
  , gpioPin(static_cast<gpio_num_t>(OPENSHOCK_ESTOP_PIN))
{
}

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin)
  : enabled(enabled)
  , gpioPin(gpioPin)
{
}

void EStopConfig::ToDefault()
{
  enabled = OpenShock::IsValidInputPin(OPENSHOCK_ESTOP_PIN);
  gpioPin = static_cast<gpio_num_t>(OPENSHOCK_ESTOP_PIN);
}

bool EStopConfig::FromFlatbuffers(const Serialization::Configuration::EStopConfig* config)
{
  if (config == nullptr) {
    ToDefault();  // Set to default if config is null
    return true;
  }

  // gpio_pin is a raw int8 from flash or rawconfig; anything that isn't "no pin" or a real GPIO means a
  // corrupt section, so fall back to the default.
  if (!Internal::Utils::FromIntGpioNum(gpioPin, config->gpio_pin())) {
    OS_LOGW(TAG, "Invalid E-Stop GPIO pin %d, using default", config->gpio_pin());
    ToDefault();
    return true;
  }

  enabled = config->enabled();
  Normalize();

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

  if (!Internal::Utils::FromJsonGpioNum(gpioPin, json, "gpioPin")) {
    gpioPin = static_cast<gpio_num_t>(OPENSHOCK_ESTOP_PIN);
  }

  if (JSON::JsonView enabledJson = json["enabled"]; enabledJson.valid()) {
    if (!enabledJson.tryGetBool(enabled)) {
      OS_LOGE(TAG, "Failed to parse enabled");
      return false;
    }
  } else {
    enabled = OpenShock::IsValidInputPin(gpioPin);
  }
  Normalize();

  return true;
}

void EStopConfig::Normalize()
{
  // An E-Stop on a pin that can't be read as an input would never trigger; store it as disabled on every path.
  if (enabled && !OpenShock::IsValidInputPin(gpioPin)) {
    OS_LOGW(TAG, "E-Stop pin %d is not a valid input pin, disabling E-Stop", gpioPin);
    enabled = false;
  }
}

void EStopConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "enabled", enabled);
  json_gen_obj_set_int(gen, "gpioPin", gpioPin);
  JSON::objEnd(gen, name);
}
