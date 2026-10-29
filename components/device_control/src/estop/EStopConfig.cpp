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
namespace Fbs = OpenShock::Serialization::Configuration;

static constexpr const char* kNamespace  = "estop";
static constexpr const char* kKeyEnabled = "enabled";
static constexpr const char* kKeyGpio    = "gpio";
static constexpr const char* kKeyLatched = "latched";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<EStopConfig> s_section({.name = "estop", .order = 6, .hubConfigField = Fbs::HubConfig::VT_ESTOP, .namespaces = kNamespaces});

Section<EStopConfig>& OpenShock::Config::EStopSection()
{
  return s_section;
}

EStopConfig::EStopConfig()
  : enabled(OpenShock::IsValidInputPin(Board::Get().estopGpio))
  , gpioPin(static_cast<gpio_num_t>(Board::Get().estopGpio))
{
}

EStopConfig::EStopConfig(bool enabled, gpio_num_t gpioPin)
  : enabled(enabled)
  , gpioPin(gpioPin)
{
}

void EStopConfig::ToDefault()
{
  enabled = OpenShock::IsValidInputPin(Board::Get().estopGpio);
  gpioPin = static_cast<gpio_num_t>(Board::Get().estopGpio);
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
  auto handle = Nvs::Handle::Open(partition, kNamespace, false);
  if (!handle) {
    ToDefault();
    return true;
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
    ToDefault();
    return true;
  }
  gpioPin = *pin;
  if (!Nvs::Load(*handle, kKeyEnabled, enabled, OpenShock::IsValidInputPin(gpioPin))) {
    return false;
  }
  Normalize();
  return true;
}

bool EStopConfig::SaveNvs(const char* partition, const EStopConfig* previous) const
{
  auto handle = Nvs::Handle::Open(partition, kNamespace, true);
  if (!handle) {
    return false;
  }
  const int8_t pin     = static_cast<int8_t>(gpioPin);
  const int8_t prevPin = previous != nullptr ? static_cast<int8_t>(previous->gpioPin) : 0;
  return handle->SetBool(kKeyEnabled, enabled, previous != nullptr ? &previous->enabled : nullptr) && handle->SetI8(kKeyGpio, pin, previous != nullptr ? &prevPin : nullptr) && handle->Commit();
}

bool EStopConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    ToDefault();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  gpioPin = Parse::JsonGpioNum(json, "gpioPin").value_or(static_cast<gpio_num_t>(Board::Get().estopGpio));

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
  Normalize();

  return true;
}

void EStopConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  (void)withSensitiveData;
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "enabled", enabled);
  json_gen_obj_set_int(gen, "gpioPin", gpioPin);
  JSON::objEnd(gen, name);
}

flatbuffers::Offset<Fbs::EStopConfig> EStopConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  (void)withSensitiveData;
  return Fbs::CreateEStopConfig(builder, enabled, gpioPin);
}

std::optional<EStopConfig> OpenShock::Config::GetEStopConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetEStopConfig(const EStopConfig& config)
{
  return s_section.Update([&](EStopConfig& estop) {
    estop = config;
    estop.Normalize();
    return true;
  });
}

std::optional<bool> OpenShock::Config::GetEStopEnabled()
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

std::optional<gpio_num_t> OpenShock::Config::GetEStopGpioPin()
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

bool OpenShock::Config::LoadEStopLatched()
{
  ScopedLock lock__(&Internal::WriteLock());  // The partition can't change or close under the read

  const char* partition = Internal::Partition();
  if (partition == nullptr) {
    return false;
  }

  auto handle = Nvs::Handle::Open(partition, kNamespace, false);
  if (!handle) {
    return false;  // No estop namespace yet: never latched
  }

  uint8_t latched = 0;
  switch (handle->GetU8(kKeyLatched, latched)) {
    case Nvs::Result::Found:
      return latched != 0;
    case Nvs::Result::Missing:
      return false;
    case Nvs::Result::Error:
    default:
      OS_LOGE(TAG, "Failed to read the E-Stop latch, treating it as latched");
      return true;
  }
}

LatchSave OpenShock::Config::SaveEStopLatched(bool latched)
{
  ScopedTryLock lock__(&Internal::WriteLock(), 0);
  if (!lock__.isLocked()) {
    return LatchSave::Busy;
  }

  const char* partition = Internal::Partition();
  if (partition == nullptr) {
    return LatchSave::Saved;
  }

  auto handle = Nvs::Handle::Open(partition, kNamespace, true);
  if (!handle || !handle->SetU8(kKeyLatched, latched ? 1 : 0) || !handle->Commit()) {
    return LatchSave::Failed;
  }

  return LatchSave::Saved;
}
