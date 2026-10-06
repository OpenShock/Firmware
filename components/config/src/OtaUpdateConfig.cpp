#include "config/OtaUpdateConfig.h"

const char* const TAG = "Config::OtaUpdateConfig";

#include "config/internal/utils.h"
#include "Logging.h"

using namespace OpenShock::Config;
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

bool OtaUpdateConfig::FromFlatbuffers(const Serialization::Configuration::OtaUpdateConfig* config)
{
  if (config == nullptr) {
    OS_LOGW(TAG, "Config is null, setting to default");
    ToDefault();
    return true;
  }

  isEnabled = config->is_enabled();
  Internal::Utils::FromFbsStr(cdnDomain, config->cdn_domain(), CONFIG_OPENSHOCK_FW_CDN_DOMAIN);
  // Out-of-range enum values (bit flips, configs from newer firmware) fall back to the defaults
  auto fbsChannel        = static_cast<uint8_t>(config->update_channel());
  updateChannel          = fbsChannel <= static_cast<uint8_t>(OtaUpdateChannel::Develop) ? static_cast<OtaUpdateChannel>(fbsChannel) : OtaUpdateChannel::Stable;
  checkOnStartup         = config->check_on_startup();
  checkPeriodically      = config->check_periodically();
  checkInterval          = config->check_interval();
  allowBackendManagement = config->allow_backend_management();
  requireManualApproval  = config->require_manual_approval();
  updateId               = config->update_id();
  auto fbsStep           = static_cast<uint8_t>(config->update_step());
  updateStep             = fbsStep <= static_cast<uint8_t>(OtaUpdateStep::RollingBack) ? static_cast<OtaUpdateStep>(fbsStep) : OtaUpdateStep::None;

  return true;
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

  if (!json["isEnabled"].tryGetBool(isEnabled)) isEnabled = true;

  if (std::string sv; json["cdnDomain"].tryGetStr(sv)) {
    cdnDomain = sv;
  } else {
    cdnDomain = CONFIG_OPENSHOCK_FW_CDN_DOMAIN;
  }

  Internal::Utils::FromJsonStrParsed(updateChannel, json, "updateChannel"sv, OpenShock::TryParseOtaUpdateChannel, OpenShock::OtaUpdateChannel::Stable);

  if (!json["checkOnStartup"].tryGetBool(checkOnStartup)) checkOnStartup = false;
  if (!json["checkPeriodically"].tryGetBool(checkPeriodically)) checkPeriodically = false;
  if (!json["checkInterval"].tryGetU16(checkInterval)) checkInterval = 30;
  if (!json["allowBackendManagement"].tryGetBool(allowBackendManagement)) allowBackendManagement = true;
  if (!json["requireManualApproval"].tryGetBool(requireManualApproval)) requireManualApproval = false;
  if (!json["updateId"].tryGetI32(updateId)) updateId = 0;

  Internal::Utils::FromJsonStrParsed(updateStep, json, "updateStep"sv, OpenShock::TryParseOtaUpdateStep, OpenShock::OtaUpdateStep::None);

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
