#include "config/OtaUpdateConfig.h"

#include "Logging.h"

const char* const TAG = "Config::OtaUpdateConfig";

using namespace OpenShock::Config;

OtaUpdateConfig::OtaUpdateConfig() {
  ToDefault();
}

OtaUpdateConfig::OtaUpdateConfig(bool isEnabled, std::string cdnDomain, std::string updateChannel, bool checkOnStartup, std::uint16_t checkInterval, bool allowBackendManagement, bool requireManualApproval) {
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
  updateChannel          = "stable";
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
  cdnDomain              = config->cdn_domain()->str();
  updateChannel          = config->update_channel()->str();
  checkOnStartup         = config->check_on_startup();
  checkInterval          = config->check_interval();
  allowBackendManagement = config->allow_backend_management();
  requireManualApproval  = config->require_manual_approval();

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::OtaUpdateConfig> OtaUpdateConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  return Serialization::Configuration::CreateOtaUpdateConfig(builder, isEnabled, builder.CreateString(cdnDomain), builder.CreateString(updateChannel), checkInterval, allowBackendManagement, requireManualApproval);
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

  const cJSON* isEnabledJson = cJSON_GetObjectItemCaseSensitive(json, "isEnabled");
  if (isEnabledJson == nullptr) {
    ESP_LOGE(TAG, "isEnabled is null");
    return false;
  }

  if (!cJSON_IsBool(isEnabledJson)) {
    ESP_LOGE(TAG, "isEnabled is not a bool");
    return false;
  }

  isEnabled = cJSON_IsTrue(isEnabledJson);

  const cJSON* cdnDomainJson = cJSON_GetObjectItemCaseSensitive(json, "cdnDomain");
  if (cdnDomainJson == nullptr) {
    ESP_LOGE(TAG, "cdnDomain is null");
    return false;
  }

  if (!cJSON_IsString(cdnDomainJson)) {
    ESP_LOGE(TAG, "cdnDomain is not a string");
    return false;
  }

  cdnDomain = cdnDomainJson->valuestring;

  const cJSON* updateChannelJson = cJSON_GetObjectItemCaseSensitive(json, "updateChannel");
  if (updateChannelJson == nullptr) {
    ESP_LOGE(TAG, "updateChannel is null");
    return false;
  }

  if (!cJSON_IsString(updateChannelJson)) {
    ESP_LOGE(TAG, "updateChannel is not a string");
    return false;
  }

  updateChannel = updateChannelJson->valuestring;

  const cJSON* checkOnStartupJson = cJSON_GetObjectItemCaseSensitive(json, "checkOnStartup");
  if (checkOnStartupJson == nullptr) {
    ESP_LOGE(TAG, "checkOnStartup is null");
    return false;
  }

  if (!cJSON_IsBool(checkOnStartupJson)) {
    ESP_LOGE(TAG, "checkOnStartup is not a bool");
    return false;
  }

  checkOnStartup = cJSON_IsTrue(checkOnStartupJson);

  const cJSON* checkIntervalJson = cJSON_GetObjectItemCaseSensitive(json, "checkInterval");
  if (checkIntervalJson == nullptr) {
    ESP_LOGE(TAG, "checkInterval is null");
    return false;
  }

  if (!cJSON_IsNumber(checkIntervalJson)) {
    ESP_LOGE(TAG, "checkInterval is not a number");
    return false;
  }

  if (checkIntervalJson->valueint < 0) {
    ESP_LOGE(TAG, "checkInterval is less than 0");
    return false;
  }
  if (checkIntervalJson->valueint > UINT16_MAX) {
    ESP_LOGE(TAG, "checkInterval is greater than UINT16_MAX");
    return false;
  }

  checkInterval = checkIntervalJson->valueint;

  const cJSON* allowBackendManagementJson = cJSON_GetObjectItemCaseSensitive(json, "allowBackendManagement");
  if (allowBackendManagementJson == nullptr) {
    ESP_LOGE(TAG, "allowBackendManagement is null");
    return false;
  }

  if (!cJSON_IsBool(allowBackendManagementJson)) {
    ESP_LOGE(TAG, "allowBackendManagement is not a bool");
    return false;
  }

  allowBackendManagement = cJSON_IsTrue(allowBackendManagementJson);

  const cJSON* requireManualApprovalJson = cJSON_GetObjectItemCaseSensitive(json, "requireManualApproval");
  if (requireManualApprovalJson == nullptr) {
    ESP_LOGE(TAG, "requireManualApproval is null");
    return false;
  }

  if (!cJSON_IsBool(requireManualApprovalJson)) {
    ESP_LOGE(TAG, "requireManualApproval is not a bool");
    return false;
  }

  requireManualApproval = cJSON_IsTrue(requireManualApprovalJson);

  return true;
}

cJSON* OtaUpdateConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "isEnabled", isEnabled);
  cJSON_AddStringToObject(root, "cdnDomain", cdnDomain.c_str());
  cJSON_AddStringToObject(root, "updateChannel", updateChannel.c_str());
  cJSON_AddBoolToObject(root, "checkOnStartup", checkOnStartup);
  cJSON_AddNumberToObject(root, "checkInterval", checkInterval);
  cJSON_AddBoolToObject(root, "allowBackendManagement", allowBackendManagement);
  cJSON_AddBoolToObject(root, "requireManualApproval", requireManualApproval);

  return root;
}
