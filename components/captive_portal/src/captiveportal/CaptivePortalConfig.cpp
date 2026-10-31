#include <freertos/FreeRTOS.h>

#include "captiveportal/CaptivePortalConfig.h"

const char* const TAG = "Config::CaptivePortalConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

using namespace OpenShock::Config;
namespace Fbs = OpenShock::Serialization::Configuration;

static constexpr const char* kNamespace = "portal";
static constexpr const char* kKeyAlways = "always";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<CaptivePortalConfig> s_section({.name = "captivePortal", .order = 2, .hubConfigField = Fbs::HubConfig::VT_CAPTIVE_PORTAL, .namespaces = kNamespaces});

Section<CaptivePortalConfig>& OpenShock::Config::CaptivePortalSection()
{
  return s_section;
}

CaptivePortalConfig::CaptivePortalConfig()
  : alwaysEnabled(false)
{
}

CaptivePortalConfig::CaptivePortalConfig(bool alwaysEnabled)
  : alwaysEnabled(alwaysEnabled)
{
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::CaptivePortalConfig> CaptivePortalConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Serialization::Configuration::CreateCaptivePortalConfig(builder, alwaysEnabled);
}

bool CaptivePortalConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    OS_LOGW(TAG, "Config is null, setting to default");
    *this = CaptivePortalConfig();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  // A field that is there but is not a bool is a bad document, not a default: accepting it would store `false` over
  // whatever the hub has.
  if (JSON::JsonView field = json["alwaysEnabled"]; field.valid()) {
    auto parsed = field.tryGetBool();
    if (!parsed) {
      OS_LOGE(TAG, "alwaysEnabled is not a boolean");
      return false;
    }
    alwaysEnabled = *parsed;
  } else {
    alwaysEnabled = false;
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
  std::optional<Nvs::Handle> handle;
  switch (Nvs::Handle::OpenRead(partition, kNamespace, handle)) {
    case Nvs::Result::Error:
      return false;
    case Nvs::Result::Missing:
      *this = CaptivePortalConfig();
      return true;
    case Nvs::Result::Found:
      break;
  }
  return Nvs::Load(*handle, kKeyAlways, alwaysEnabled, false);
}

bool CaptivePortalConfig::SaveNvs(const char* partition, const CaptivePortalConfig* previous) const
{
  auto handle = Nvs::Handle::Open(partition, kNamespace, true);
  if (!handle) {
    return false;
  }
  return handle->SetBool(kKeyAlways, alwaysEnabled, previous != nullptr ? &previous->alwaysEnabled : nullptr) && handle->Commit();
}

std::optional<CaptivePortalConfig> OpenShock::Config::GetCaptivePortalConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetCaptivePortalConfig(const CaptivePortalConfig& config)
{
  return s_section.Set(config);
}
