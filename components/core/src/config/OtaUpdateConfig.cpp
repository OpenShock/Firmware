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
  updateChannel          = static_cast<OtaUpdateChannel>(config->update_channel());
  checkOnStartup         = config->check_on_startup();
  checkPeriodically      = config->check_periodically();
  checkInterval          = config->check_interval();
  allowBackendManagement = config->allow_backend_management();
  requireManualApproval  = config->require_manual_approval();
  updateId               = config->update_id();
  updateStep             = static_cast<OtaUpdateStep>(config->update_step());

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::OtaUpdateConfig> OtaUpdateConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Serialization::Configuration::CreateOtaUpdateConfig(
    builder,
    isEnabled,
    builder.CreateString(cdnDomain),
    static_cast<Serialization::Configuration::OtaUpdateChannel>(updateChannel),
    checkOnStartup,
    checkPeriodically,
    checkInterval,
    allowBackendManagement,
    requireManualApproval,
    updateId,
    static_cast<Serialization::Configuration::OtaUpdateStep>(updateStep)
  );
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

  Internal::Utils::FromJsonBool(isEnabled, json, "isEnabled", true);
  Internal::Utils::FromJsonStr(cdnDomain, json, "cdnDomain", CONFIG_OPENSHOCK_FW_CDN_DOMAIN);
  Internal::Utils::FromJsonStrParsed(updateChannel, json, "updateChannel"sv, OpenShock::TryParseOtaUpdateChannel, OpenShock::OtaUpdateChannel::Stable);
  Internal::Utils::FromJsonBool(checkOnStartup, json, "checkOnStartup", false);
  Internal::Utils::FromJsonBool(checkPeriodically, json, "checkPeriodically", false);
  Internal::Utils::FromJsonU16(checkInterval, json, "checkInterval", 30);
  Internal::Utils::FromJsonBool(allowBackendManagement, json, "allowBackendManagement", true);
  Internal::Utils::FromJsonBool(requireManualApproval, json, "requireManualApproval", false);
  Internal::Utils::FromJsonI32(updateId, json, "updateId", 0);
  Internal::Utils::FromJsonStrParsed(updateStep, json, "updateStep"sv, OpenShock::TryParseOtaUpdateStep, OpenShock::OtaUpdateStep::None);

  return true;
}

void OtaUpdateConfig::ToJSON(json_gen_str_t* gen, bool withSensitiveData) const
{
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
}
