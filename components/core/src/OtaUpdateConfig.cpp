#include <freertos/FreeRTOS.h>

#include "OtaUpdateConfig.h"

const char* const TAG = "Config::OtaUpdateConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

#include <utility>

using namespace OpenShock::Config;
namespace Fbs = OpenShock::Serialization::Configuration;

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

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<OtaUpdateConfig> s_section({.name = "otaUpdate", .order = 5, .hubConfigField = Fbs::HubConfig::VT_OTA_UPDATE, .namespaces = kNamespaces});

Section<OtaUpdateConfig>& OpenShock::Config::OtaUpdateSection()
{
  return s_section;
}

using namespace std::string_view_literals;

OtaUpdateConfig::OtaUpdateConfig()
  : isEnabled(true)
  , cdnDomain(CONFIG_OPENSHOCK_FW_CDN_DOMAIN)
  , updateChannel(OtaUpdateChannel::Stable)
  , checkOnStartup(false)
  , checkPeriodically(false)
  , checkInterval(30)
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

void OtaUpdateConfig::ToDefault()
{
  isEnabled              = true;
  cdnDomain              = CONFIG_OPENSHOCK_FW_CDN_DOMAIN;
  updateChannel          = OtaUpdateChannel::Stable;
  checkOnStartup         = false;
  checkPeriodically      = false;
  checkInterval          = 30;  // 30 minutes
  allowBackendManagement = true;
  requireManualApproval  = false;
  updateId               = 0;
  updateStep             = OtaUpdateStep::None;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::OtaUpdateConfig> OtaUpdateConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  namespace Fbs = Serialization::Configuration;

  auto cdnDomainOffset = builder.CreateString(cdnDomain);

  // Same field order as CreateOtaUpdateConfig, except that is_enabled and
  // allow_backend_management are stored even when they equal the schema default, so
  // they read back the same under every schema version (up to 1.5.x both defaulted
  // to false) and RootConfig::FromFlatbuffers never has to infer them.
  Fbs::OtaUpdateConfigBuilder otaBuilder(builder);
  otaBuilder.add_update_id(updateId);
  otaBuilder.add_cdn_domain(cdnDomainOffset);
  otaBuilder.add_check_interval(checkInterval);
  otaBuilder.add_update_step(static_cast<Fbs::OtaUpdateStep>(updateStep));
  otaBuilder.add_require_manual_approval(requireManualApproval);
  builder.AddElement<uint8_t>(Fbs::OtaUpdateConfig::VT_ALLOW_BACKEND_MANAGEMENT, static_cast<uint8_t>(allowBackendManagement));
  otaBuilder.add_check_periodically(checkPeriodically);
  otaBuilder.add_check_on_startup(checkOnStartup);
  otaBuilder.add_update_channel(static_cast<Fbs::OtaUpdateChannel>(updateChannel));
  builder.AddElement<uint8_t>(Fbs::OtaUpdateConfig::VT_IS_ENABLED, static_cast<uint8_t>(isEnabled));
  return otaBuilder.Finish();
}

bool OtaUpdateConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
  }

  isEnabled = json["isEnabled"].tryGetBool().value_or(true);

  cdnDomain = json["cdnDomain"].tryGetStr().value_or(CONFIG_OPENSHOCK_FW_CDN_DOMAIN);

  Parse::JsonStrParsed(updateChannel, json, "updateChannel"sv, OpenShock::TryParseOtaUpdateChannel, OpenShock::OtaUpdateChannel::Stable);

  checkOnStartup         = json["checkOnStartup"].tryGetBool().value_or(false);
  checkPeriodically      = json["checkPeriodically"].tryGetBool().value_or(false);
  checkInterval          = json["checkInterval"].tryGetU16().value_or(30);
  allowBackendManagement = json["allowBackendManagement"].tryGetBool().value_or(true);
  requireManualApproval  = json["requireManualApproval"].tryGetBool().value_or(false);
  updateId               = json["updateId"].tryGetI32().value_or(0);

  Parse::JsonStrParsed(updateStep, json, "updateStep"sv, OpenShock::TryParseOtaUpdateStep, OpenShock::OtaUpdateStep::None);

  return true;
}

void OtaUpdateConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "isEnabled", isEnabled);
  JSON::objSetString(gen, "cdnDomain", cdnDomain);
  JSON::objSetString(gen, "updateChannel", OpenShock::Serialization::Configuration::EnumNameOtaUpdateChannel(static_cast<Serialization::Configuration::OtaUpdateChannel>(updateChannel)));
  json_gen_obj_set_bool(gen, "checkOnStartup", checkOnStartup);
  json_gen_obj_set_bool(gen, "checkPeriodically", checkPeriodically);
  json_gen_obj_set_int(gen, "checkInterval", checkInterval);
  json_gen_obj_set_bool(gen, "allowBackendManagement", allowBackendManagement);
  json_gen_obj_set_bool(gen, "requireManualApproval", requireManualApproval);
  json_gen_obj_set_int(gen, "updateId", updateId);
  JSON::objSetString(gen, "updateStep", OpenShock::Serialization::Configuration::EnumNameOtaUpdateStep(static_cast<Serialization::Configuration::OtaUpdateStep>(updateStep)));
  JSON::objEnd(gen, name);
}

bool OtaUpdateConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::Open(partition, kNamespace, false);
  if (!handle) {
    ToDefault();
    return true;
  }

  uint8_t channel = std::to_underlying(OtaUpdateChannel::Stable);
  uint8_t step    = std::to_underlying(OtaUpdateStep::None);
  bool ok = Nvs::Load(*handle, kKeyEnabled, isEnabled, true) && Nvs::Load(*handle, kKeyDomain, cdnDomain, CONFIG_OPENSHOCK_FW_CDN_DOMAIN) && Nvs::Load(*handle, kKeyChannel, channel, channel) && Nvs::Load(*handle, kKeyCheckBoot, checkOnStartup, false)
         && Nvs::Load(*handle, kKeyCheckPeriod, checkPeriodically, false) && Nvs::Load(*handle, kKeyCheckMin, checkInterval, static_cast<uint16_t>(30)) && Nvs::Load(*handle, kKeyBackendMgmt, allowBackendManagement, true)
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
  auto handle = Nvs::Handle::Open(partition, kNamespace, true);
  if (!handle) {
    return false;
  }
  const uint8_t channel     = std::to_underlying(updateChannel);
  const uint8_t step        = std::to_underlying(updateStep);
  const uint8_t prevChannel = previous != nullptr ? std::to_underlying(previous->updateChannel) : 0;
  const uint8_t prevStep    = previous != nullptr ? std::to_underlying(previous->updateStep) : 0;
  const bool p              = previous != nullptr;
  return handle->SetBool(kKeyEnabled, isEnabled, p ? &previous->isEnabled : nullptr) && handle->SetStr(kKeyDomain, cdnDomain, p ? &previous->cdnDomain : nullptr) && handle->SetU8(kKeyChannel, channel, p ? &prevChannel : nullptr)
      && handle->SetBool(kKeyCheckBoot, checkOnStartup, p ? &previous->checkOnStartup : nullptr) && handle->SetBool(kKeyCheckPeriod, checkPeriodically, p ? &previous->checkPeriodically : nullptr)
      && handle->SetU16(kKeyCheckMin, checkInterval, p ? &previous->checkInterval : nullptr) && handle->SetBool(kKeyBackendMgmt, allowBackendManagement, p ? &previous->allowBackendManagement : nullptr)
      && handle->SetBool(kKeyManual, requireManualApproval, p ? &previous->requireManualApproval : nullptr) && handle->SetI32(kKeyUpdateId, updateId, p ? &previous->updateId : nullptr) && handle->SetU8(kKeyStep, step, p ? &prevStep : nullptr)
      && handle->Commit();
}

std::optional<OtaUpdateConfig> OpenShock::Config::GetOtaUpdateConfig()
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

std::optional<int32_t> OpenShock::Config::GetOtaUpdateId()
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

std::optional<OpenShock::OtaUpdateStep> OpenShock::Config::GetOtaUpdateStep()
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
