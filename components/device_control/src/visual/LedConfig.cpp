#include <freertos/FreeRTOS.h>

#include "visual/LedConfig.h"

const char* const TAG = "Config::LedConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

using namespace OpenShock::Config;

static constexpr const char* kNamespace = "led";
static constexpr const char* kKeyStatus = "status";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<LedConfig> s_section({.name = "led", .order = 7, .namespaces = kNamespaces});

LedConfig::LedConfig()
  : statusEnabled(true)
{
}

LedConfig::LedConfig(bool statusEnabled)
  : statusEnabled(statusEnabled)
{
}

bool LedConfig::FromJSON(JSON::JsonView json)
{
  if (auto done = Parse::ObjectOrDefault(*this, json, TAG)) {
    return *done;
  }

  if (!Parse::JsonBool(statusEnabled, json, "statusEnabled", true, TAG)) {
    return false;
  }

  return true;
}

void LedConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  (void)withSensitiveData;
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "statusEnabled", statusEnabled);
  JSON::objEnd(gen, name);
}

bool LedConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::OpenForLoad(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Load(*handle, kKeyStatus, statusEnabled, true);
}

bool LedConfig::SaveNvs(const char* partition, const LedConfig* previous) const
{
  auto handle = Nvs::Handle::OpenForWrite(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Saver(*handle, *this, previous).Set(kKeyStatus, &LedConfig::statusEnabled).Commit();
}

LedConfig OpenShock::Config::GetLedConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetLedConfig(const LedConfig& config)
{
  return s_section.Set(config);
}

bool OpenShock::Config::GetLedStatusEnabled()
{
  return s_section.Read([](const LedConfig& led) { return led.statusEnabled; });
}

bool OpenShock::Config::SetLedStatusEnabled(bool enabled)
{
  return s_section.Update([&](LedConfig& led) {
    led.statusEnabled = enabled;
    return true;
  });
}
