#include "config/EStopConfig.h"

#include "BoardDescriptor.h"
#include "Chipset.h"
#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::EStopConfig";

using namespace OpenShock::Config;

// The board's default switch type (OPENSHOCK_ESTOP_LATCHING in boards/<board>.defaults, stamped into the descriptor).
static bool defaultLatching()
{
  return (OpenShock::Board::Get().flags & OPENSHOCK_BOARD_FLAG_ESTOP_LATCHING) != 0;
}

EStopConfig::EStopConfig()
  : enabled(OpenShock::IsValidInputPin(Board::Get().estopGpio))
  , gpioPin(static_cast<gpio_num_t>(Board::Get().estopGpio))
  , latching(defaultLatching())
  , active(false)
{
}

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin)
  : EStopConfig(enabled, gpioPin, defaultLatching(), false)
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
  enabled  = OpenShock::IsValidInputPin(Board::Get().estopGpio);
  gpioPin  = static_cast<gpio_num_t>(Board::Get().estopGpio);
  latching = defaultLatching();
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
  auto pin = Internal::Utils::FromIntGpioNum(config->gpio_pin());
  if (!pin) {
    OS_LOGW(TAG, "Invalid E-Stop GPIO pin %d, using default", config->gpio_pin());
    ToDefault();
    latching = config->latching();
    active   = config->active();  // Never drop an active E-Stop, even when the rest of the section is unusable
    return true;
  }

  gpioPin  = *pin;
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

  gpioPin = Internal::Utils::FromJsonGpioNum(json, "gpioPin").value_or(static_cast<gpio_num_t>(Board::Get().estopGpio));

  if (JSON::JsonView enabledJson = json["enabled"]; enabledJson.valid()) {
    auto parsed = enabledJson.tryGetBool();
    if (!parsed) {
      OS_LOGE(TAG, "Failed to parse enabled");
      return false;
    }
    enabled = *parsed;
  } else {
    enabled = OpenShock::IsValidInputPin(gpioPin);
  }

  if (JSON::JsonView latchingJson = json["latching"]; latchingJson.valid()) {
    auto parsed = latchingJson.tryGetBool();
    if (!parsed) {
      OS_LOGE(TAG, "Failed to parse latching");
      return false;
    }
    latching = *parsed;
  } else {
    latching = defaultLatching();
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
