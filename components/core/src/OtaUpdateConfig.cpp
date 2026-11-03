#include <freertos/FreeRTOS.h>

#include "OtaUpdateConfig.h"

const char* const TAG = "Config::OtaUpdateConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

using namespace OpenShock::Config;

static constexpr const char* kNamespace      = "ota";
static constexpr const char* kKeyEnabled     = "enabled";
static constexpr const char* kKeyDomain      = "domain";
static constexpr const char* kKeyChannel     = "channel";
static constexpr const char* kKeyCheckBoot   = "chk_boot";
static constexpr const char* kKeyCheckPeriod = "chk_period";
static constexpr const char* kKeyCheckMin    = "chk_min";
static constexpr const char* kKeyBackendMgmt = "backend_mgmt";
static constexpr const char* kKeyManual      = "manual";
static constexpr const char* kKeyUpdateId    = "update_id";
static constexpr const char* kKeyStep        = "step";

static constexpr uint16_t kDefaultCheckInterval = 30;  // minutes

// The domains of the 1.x firmware CDN: they serve flat files for 1.x hubs, not the repository API this firmware asks for.
static constexpr std::string_view kLegacyCdnDomains[] = {"firmware.openshock.org", "fw.openshock.org"};

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<OtaUpdateConfig> s_section({.name = "otaUpdate", .order = 5, .namespaces = kNamespaces});

OtaUpdateConfig::OtaUpdateConfig()
  : isEnabled(true)
  , cdnDomain(CONFIG_OPENSHOCK_FW_CDN_DOMAIN)
  , updateChannel(OtaUpdateChannel::Stable)
  , checkOnStartup(false)
  , checkPeriodically(false)
  , checkInterval(kDefaultCheckInterval)
  , allowBackendManagement(true)
  , requireManualApproval(false)
  , updateId(0)
  , updateStep(OtaUpdateStep::None)
{
}

OtaUpdateConfig::OtaUpdateConfig(
  bool isEnabled, std::string cdnDomain, OtaUpdateChannel updateChannel, bool checkOnStartup, bool checkPeriodically, uint16_t checkInterval, bool allowBackendManagement, bool requireManualApproval, int32_t updateId, OtaUpdateStep updateStep
)
  : isEnabled(isEnabled)
  , cdnDomain(std::move(cdnDomain))
  , updateChannel(updateChannel)
  , checkOnStartup(checkOnStartup)
  , checkPeriodically(checkPeriodically)
  , checkInterval(checkInterval)
  , allowBackendManagement(allowBackendManagement)
  , requireManualApproval(requireManualApproval)
  , updateId(updateId)
  , updateStep(updateStep)
{
}

bool OtaUpdateConfig::FromJSON(JSON::JsonView json)
{
  if (auto done = Parse::ObjectOrDefault(*this, json, TAG)) {
    return *done;
  }

  // Every boolean is strict (Parse::JsonBool); the numbers and strings take their default for a value of the wrong type
  if (!Parse::JsonBool(isEnabled, json, "isEnabled", true, TAG) || !Parse::JsonBool(checkOnStartup, json, "checkOnStartup", false, TAG) || !Parse::JsonBool(checkPeriodically, json, "checkPeriodically", false, TAG)
      || !Parse::JsonBool(allowBackendManagement, json, "allowBackendManagement", true, TAG) || !Parse::JsonBool(requireManualApproval, json, "requireManualApproval", false, TAG))
  {
    return false;
  }

  cdnDomain = json["cdnDomain"].tryGetStr().value_or(CONFIG_OPENSHOCK_FW_CDN_DOMAIN);
  Parse::JsonStrParsed(updateChannel, json, "updateChannel", OpenShock::TryParseOtaUpdateChannel, OpenShock::OtaUpdateChannel::Stable);
  checkInterval = json["checkInterval"].tryGetU16().value_or(kDefaultCheckInterval);
  updateId      = json["updateId"].tryGetI32().value_or(0);
  Parse::JsonStrParsed(updateStep, json, "updateStep", OpenShock::TryParseOtaUpdateStep, OpenShock::OtaUpdateStep::None);

  return true;
}

void OtaUpdateConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "isEnabled", isEnabled);
  JSON::objSetString(gen, "cdnDomain", cdnDomain);
  JSON::objSetString(gen, "updateChannel", OpenShock::OtaUpdateChannelName(updateChannel));
  json_gen_obj_set_bool(gen, "checkOnStartup", checkOnStartup);
  json_gen_obj_set_bool(gen, "checkPeriodically", checkPeriodically);
  json_gen_obj_set_int(gen, "checkInterval", checkInterval);
  json_gen_obj_set_bool(gen, "allowBackendManagement", allowBackendManagement);
  json_gen_obj_set_bool(gen, "requireManualApproval", requireManualApproval);
  json_gen_obj_set_int(gen, "updateId", updateId);
  JSON::objSetString(gen, "updateStep", OpenShock::OtaUpdateStepName(updateStep));
  JSON::objEnd(gen, name);
}

void OtaUpdateConfig::Normalize()
{
  if (checkInterval == 0) {
    OS_LOGW(TAG, "A zero update check interval is not allowed, using the default");
    checkInterval = kDefaultCheckInterval;
  }
  // Rewritten on every load rather than once: it needs no migration state, and a hub that restores an old config still
  // lands here. Remove it once no hub or config backup can hold the 1.x domain.
  if (std::ranges::find(kLegacyCdnDomains, cdnDomain) != std::end(kLegacyCdnDomains)) {
    OS_LOGI(TAG, "Moving OTA updates from the 1.x CDN to %s", CONFIG_OPENSHOCK_FW_CDN_DOMAIN);
    cdnDomain = CONFIG_OPENSHOCK_FW_CDN_DOMAIN;
  }
}

bool OtaUpdateConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::OpenForLoad(partition, kNamespace);
  if (!handle) {
    return false;
  }

  uint8_t channel = std::to_underlying(OtaUpdateChannel::Stable);
  uint8_t step    = std::to_underlying(OtaUpdateStep::None);
  bool ok = Nvs::Load(*handle, kKeyEnabled, isEnabled, true) && Nvs::Load(*handle, kKeyDomain, cdnDomain, CONFIG_OPENSHOCK_FW_CDN_DOMAIN) && Nvs::Load(*handle, kKeyChannel, channel, channel) && Nvs::Load(*handle, kKeyCheckBoot, checkOnStartup, false)
         && Nvs::Load(*handle, kKeyCheckPeriod, checkPeriodically, false) && Nvs::Load(*handle, kKeyCheckMin, checkInterval, kDefaultCheckInterval) && Nvs::Load(*handle, kKeyBackendMgmt, allowBackendManagement, true)
         && Nvs::Load(*handle, kKeyManual, requireManualApproval, false) && Nvs::Load(*handle, kKeyUpdateId, updateId, 0) && Nvs::Load(*handle, kKeyStep, step, step);
  if (!ok) {
    return false;
  }
  // Out-of-range enum values (bit flips, configs from newer firmware) fall back to the defaults
  updateChannel = channel <= std::to_underlying(OtaUpdateChannel::Develop) ? static_cast<OtaUpdateChannel>(channel) : OtaUpdateChannel::Stable;
  updateStep    = step <= std::to_underlying(OtaUpdateStep::RollingBack) ? static_cast<OtaUpdateStep>(step) : OtaUpdateStep::None;
  return true;
}

bool OtaUpdateConfig::SaveNvs(const char* partition, const OtaUpdateConfig* previous) const
{
  auto handle = Nvs::Handle::OpenForWrite(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Saver(*handle, *this, previous)
    .Set(kKeyEnabled, &OtaUpdateConfig::isEnabled)
    .Set(kKeyDomain, &OtaUpdateConfig::cdnDomain)
    .Set<uint8_t>(kKeyChannel, &OtaUpdateConfig::updateChannel)
    .Set(kKeyCheckBoot, &OtaUpdateConfig::checkOnStartup)
    .Set(kKeyCheckPeriod, &OtaUpdateConfig::checkPeriodically)
    .Set(kKeyCheckMin, &OtaUpdateConfig::checkInterval)
    .Set(kKeyBackendMgmt, &OtaUpdateConfig::allowBackendManagement)
    .Set(kKeyManual, &OtaUpdateConfig::requireManualApproval)
    .Set(kKeyUpdateId, &OtaUpdateConfig::updateId)
    .Set<uint8_t>(kKeyStep, &OtaUpdateConfig::updateStep)
    .Commit();
}

OtaUpdateConfig OpenShock::Config::GetOtaUpdateConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetOtaUpdateConfig(const OtaUpdateConfig& config)
{
  return s_section.Set(config);
}

bool OpenShock::Config::UpdateOtaUpdateConfig(Util::FunctionRef<bool(OtaUpdateConfig&)> update)
{
  return s_section.Update(update);
}

int32_t OpenShock::Config::GetOtaUpdateId()
{
  return s_section.Read([](const OtaUpdateConfig& ota) { return ota.updateId; });
}

bool OpenShock::Config::SetOtaUpdateId(int32_t updateId)
{
  return s_section.Update([&](OtaUpdateConfig& ota) {
    if (ota.updateId == updateId) {
      return false;
    }
    ota.updateId = updateId;
    return true;
  });
}

OpenShock::OtaUpdateStep OpenShock::Config::GetOtaUpdateStep()
{
  return s_section.Read([](const OtaUpdateConfig& ota) { return ota.updateStep; });
}

bool OpenShock::Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep updateStep)
{
  return s_section.Update([&](OtaUpdateConfig& ota) {
    if (ota.updateStep == updateStep) {
      return false;
    }
    ota.updateStep = updateStep;
    return true;
  });
}

bool OpenShock::Config::CompareExchangeOtaUpdateStep(OpenShock::OtaUpdateStep expected, OpenShock::OtaUpdateStep desired)
{
  // Writers are serialized by the write lock, so the step can't change between this check and the save
  bool matched = false;
  bool saved   = s_section.Update([&](OtaUpdateConfig& ota) {
    if (ota.updateStep != expected) {
      return false;
    }
    matched = true;
    if (expected == desired) {
      return false;
    }
    ota.updateStep = desired;
    return true;
  });
  return matched && saved;
}
