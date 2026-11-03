#include <freertos/FreeRTOS.h>

#include "captiveportal/CaptivePortalConfig.h"

const char* const TAG = "Config::CaptivePortalConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

using namespace OpenShock::Config;

static constexpr const char* kNamespace = "portal";
static constexpr const char* kKeyAlways = "always";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<CaptivePortalConfig> s_section({.name = "captivePortal", .order = 2, .namespaces = kNamespaces});

CaptivePortalConfig::CaptivePortalConfig()
  : alwaysEnabled(false)
{
}

CaptivePortalConfig::CaptivePortalConfig(bool alwaysEnabled)
  : alwaysEnabled(alwaysEnabled)
{
}

bool CaptivePortalConfig::FromJSON(JSON::JsonView json)
{
  if (auto done = Parse::ObjectOrDefault(*this, json, TAG)) {
    return *done;
  }

  if (!Parse::JsonBool(alwaysEnabled, json, "alwaysEnabled", false, TAG)) {
    return false;
  }

  return true;
}

void CaptivePortalConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "alwaysEnabled", alwaysEnabled);
  JSON::objEnd(gen, name);
}

bool CaptivePortalConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::OpenForLoad(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Load(*handle, kKeyAlways, alwaysEnabled, false);
}

bool CaptivePortalConfig::SaveNvs(const char* partition, const CaptivePortalConfig* previous) const
{
  auto handle = Nvs::Handle::OpenForWrite(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Saver(*handle, *this, previous).Set(kKeyAlways, &CaptivePortalConfig::alwaysEnabled).Commit();
}

CaptivePortalConfig OpenShock::Config::GetCaptivePortalConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetCaptivePortalConfig(const CaptivePortalConfig& config)
{
  return s_section.Set(config);
}
