#include <freertos/FreeRTOS.h>

#include "estop/EStopConfig.h"

const char* const TAG = "Config::EStopConfig";

#include "BoardDescriptor.h"
#include "Chipset.h"
#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

using namespace OpenShock::Config;

static constexpr const char* kNamespace   = "estop";
static constexpr const char* kKeyEnabled  = "enabled";
static constexpr const char* kKeyGpio     = "gpio";
static constexpr const char* kKeyLatching = "latching";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<EStopConfig> s_section({.name = "estop", .order = 6, .namespaces = kNamespaces});

// The board's default switch type (OPENSHOCK_ESTOP_LATCHING in boards/<board>.defaults, stamped into the descriptor).
static bool defaultLatching()
{
  return (OpenShock::Board::Get().flags & OPENSHOCK_BOARD_FLAG_ESTOP_LATCHING) != 0;
}

EStopConfig::EStopConfig()
  : enabled(OpenShock::IsValidInputPin(Board::Get().estopGpio))
  , gpioPin(static_cast<gpio_num_t>(Board::Get().estopGpio))
  , latching(defaultLatching())
{
}

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin)
  : EStopConfig(enabled, gpioPin, defaultLatching())
{
}

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin, bool latching)
  : enabled(enabled)
  , gpioPin(gpioPin)
  , latching(latching)
{
}

void EStopConfig::Normalize()
{
  // An E-Stop on a pin that can't be read as an input would never trigger; store it as disabled on every path.
  if (enabled && !OpenShock::IsValidInputPin(gpioPin)) {
    OS_LOGW(TAG, "E-Stop pin %d is not a valid input pin, disabling E-Stop", gpioPin);
    enabled = false;
  }
}

bool EStopConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::OpenForLoad(partition, kNamespace);
  if (!handle) {
    return false;
  }

  // The switch type first: it is kept when a corrupt pin resets the rest of the section below.
  bool storedLatching = defaultLatching();
  if (!Nvs::Load(*handle, kKeyLatching, storedLatching, defaultLatching())) {
    return false;
  }

  const int8_t boardPin = static_cast<int8_t>(Board::Get().estopGpio);
  int8_t storedPin      = boardPin;
  if (!Nvs::Load(*handle, kKeyGpio, storedPin, boardPin)) {
    return false;
  }
  // Anything that isn't "no pin" or a real GPIO means a corrupt value, so the whole section falls back.
  auto pin = Parse::GpioNum(storedPin);
  if (!pin) {
    OS_LOGW(TAG, "Invalid E-Stop GPIO pin %d, using default", storedPin);
    *this    = EStopConfig();
    latching = storedLatching;
    return true;
  }
  gpioPin  = *pin;
  latching = storedLatching;
  return Nvs::Load(*handle, kKeyEnabled, enabled, OpenShock::IsValidInputPin(gpioPin));
}

bool EStopConfig::SaveNvs(const char* partition, const EStopConfig* previous) const
{
  auto handle = Nvs::Handle::OpenForWrite(partition, kNamespace);
  if (!handle) {
    return false;
  }
  // The pin first: NVS commits key by key, so a power cut between the two leaves the old enable state on the new pin
  // (disabled stays disabled; enabled moves to the pin that was asked for), never the E-Stop enabled on the old pin
  return Nvs::Saver(*handle, *this, previous).Set<int8_t>(kKeyGpio, &EStopConfig::gpioPin).Set(kKeyEnabled, &EStopConfig::enabled).Set(kKeyLatching, &EStopConfig::latching).Commit();
}

bool EStopConfig::FromJSON(JSON::JsonView json)
{
  if (auto done = Parse::ObjectOrDefault(*this, json, TAG)) {
    return *done;
  }

  gpioPin = Parse::JsonGpioNum(json, "gpioPin").value_or(static_cast<gpio_num_t>(Board::Get().estopGpio));

  if (!Parse::JsonBool(enabled, json, "enabled", OpenShock::IsValidInputPin(gpioPin), TAG)) {
    return false;
  }

  if (!Parse::JsonBool(latching, json, "latching", defaultLatching(), TAG)) {
    return false;
  }

  return true;
}

void EStopConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  (void)withSensitiveData;
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "enabled", enabled);
  json_gen_obj_set_int(gen, "gpioPin", gpioPin);
  json_gen_obj_set_bool(gen, "latching", latching);
  JSON::objEnd(gen, name);
}

EStopConfig OpenShock::Config::GetEStopConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetEStopConfig(const EStopConfig& config)
{
  return s_section.Set(config);
}

bool OpenShock::Config::GetEStopEnabled()
{
  return s_section.Read([](const EStopConfig& estop) { return estop.enabled; });
}

bool OpenShock::Config::SetEStopEnabled(bool enabled)
{
  return s_section.Update([&](EStopConfig& estop) {
    estop.enabled = enabled;
    return true;
  });
}

gpio_num_t OpenShock::Config::GetEStopGpioPin()
{
  return s_section.Read([](const EStopConfig& estop) { return estop.gpioPin; });
}

bool OpenShock::Config::SetEStopGpioPin(gpio_num_t gpioPin)
{
  if (!OpenShock::IsValidInputPin(gpioPin)) {
    OS_LOGE(TAG, "Invalid EStop GPIO Pin: %d", gpioPin);
    return false;
  }

  return s_section.Update([&](EStopConfig& estop) {
    estop.gpioPin = gpioPin;
    return true;
  });
}

bool OpenShock::Config::GetEStopLatching()
{
  return s_section.Read([](const EStopConfig& estop) { return estop.latching; });
}

bool OpenShock::Config::SetEStopLatching(bool latching)
{
  return s_section.Update([&](EStopConfig& estop) {
    estop.latching = latching;
    return true;
  });
}
