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
  , latching(OPENSHOCK_ESTOP_LATCHING)
  , active(false)
{
}

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin)
  : EStopConfig(enabled, gpioPin, OPENSHOCK_ESTOP_LATCHING, false)
{
}

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin, bool latching, bool active)
  : enabled(enabled)
  , gpioPin(gpioPin)
  , latching(latching)
  , active(active)
{
}

void EStopConfig::ToDefault()
{
  enabled  = OpenShock::IsValidInputPin(OPENSHOCK_ESTOP_PIN);
  gpioPin  = static_cast<gpio_num_t>(OPENSHOCK_ESTOP_PIN);
  latching = OPENSHOCK_ESTOP_LATCHING;
  active   = false;
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
    latching = config->latching();
    active   = config->active();  // Never drop an active E-Stop, even when the rest of the section is unusable
    return true;
  }

  enabled  = config->enabled();
  latching = config->latching();
  active   = config->active();
  Normalize();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::EStopConfig> EStopConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Serialization::Configuration::CreateEStopConfig(builder, enabled, gpioPin, active, latching);
}

// `active` is owned by EStopManager (persisted on every state change, restored on boot). JSON import never touches it,
// so a jsonconfig can't release an active E-Stop across a reboot.
bool EStopConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    bool wasActive = active;
    ToDefault();  // Set to default if config is null
    active = wasActive;
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

  if (JSON::JsonView latchingJson = json["latching"]; latchingJson.valid()) {
    if (!latchingJson.tryGetBool(latching)) {
      OS_LOGE(TAG, "Failed to parse latching");
      return false;
    }
  } else {
    latching = OPENSHOCK_ESTOP_LATCHING;
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
  json_gen_obj_set_bool(gen, "latching", latching);
  json_gen_obj_set_bool(gen, "active", active);
  JSON::objEnd(gen, name);
}
