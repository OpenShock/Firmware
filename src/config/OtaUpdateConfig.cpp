#include "config/OtaUpdateConfig.h"

#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::OtaUpdateConfig";

using namespace OpenShock::Config;

OtaUpdateConfig::OtaUpdateConfig() {
  ToDefault();
}

OtaUpdateConfig::OtaUpdateConfig(bool isEnabled, std::string cdnDomain, OtaUpdateChannel updateChannel, bool checkOnStartup, std::uint16_t checkInterval, bool allowBackendManagement, bool requireManualApproval) {
  this->isEnabled              = isEnabled;
  this->cdnDomain              = cdnDomain;
  this->updateChannel          = updateChannel;
  this->checkOnStartup         = checkOnStartup;
  this->checkInterval          = checkInterval;
  this->allowBackendManagement = allowBackendManagement;
  this->requireManualApproval  = requireManualApproval;
}

void OtaUpdateConfig::ToDefault() {
  isEnabled              = true;
  cdnDomain              = OPENSHOCK_FW_CDN_DOMAIN;
  updateChannel          = OtaUpdateChannel::Stable;
  checkOnStartup         = true;
  checkInterval          = 0;
  allowBackendManagement = true;
  requireManualApproval  = false;
}

bool OtaUpdateConfig::FromFlatbuffers(const Serialization::Configuration::OtaUpdateConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  isEnabled              = config->is_enabled();
  Internal::Utils::FromFbsStr(cdnDomain, config->cdn_domain(), OPENSHOCK_FW_CDN_DOMAIN);
  updateChannel          = config->update_channel();
  checkOnStartup         = config->check_on_startup();
  checkInterval          = config->check_interval();
  allowBackendManagement = config->allow_backend_management();
  requireManualApproval  = config->require_manual_approval();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::OtaUpdateConfig> OtaUpdateConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  return Serialization::Configuration::CreateOtaUpdateConfig(builder, isEnabled, builder.CreateString(cdnDomain), updateChannel, checkInterval, allowBackendManagement, requireManualApproval);
}

bool OtaUpdateConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (!cJSON_IsObject(json)) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  Internal::Utils::FromJsonBool(isEnabled, json, "isEnabled", true);
  Internal::Utils::FromJsonStr(cdnDomain, json, "cdnDomain", OPENSHOCK_FW_CDN_DOMAIN);
  Internal::Utils::FromJsonStrParsed(updateChannel, json, "updateChannel", OpenShock::TryParseOtaUpdateChannel, OpenShock::OtaUpdateChannel::Stable);
  Internal::Utils::FromJsonBool(checkOnStartup, json, "checkOnStartup", true);
  Internal::Utils::FromJsonU16(checkInterval, json, "checkInterval", 0);
  Internal::Utils::FromJsonBool(allowBackendManagement, json, "allowBackendManagement", true);
  Internal::Utils::FromJsonBool(requireManualApproval, json, "requireManualApproval", false);

  return true;
}

cJSON* OtaUpdateConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "isEnabled", isEnabled);
  cJSON_AddStringToObject(root, "cdnDomain", cdnDomain.c_str());
  cJSON_AddStringToObject(root, "updateChannel", OpenShock::Serialization::Configuration::EnumNameOtaUpdateChannel(updateChannel));
  cJSON_AddBoolToObject(root, "checkOnStartup", checkOnStartup);
  cJSON_AddNumberToObject(root, "checkInterval", checkInterval);
  cJSON_AddBoolToObject(root, "allowBackendManagement", allowBackendManagement);
  cJSON_AddBoolToObject(root, "requireManualApproval", requireManualApproval);

  return root;
}
