#include "config/RootConfig.h"

const char* const TAG = "Config::RootConfig";

#include "Logging.h"

using namespace OpenShock::Config;

void RootConfig::ToDefault() {
  rf.ToDefault();
  wifi.ToDefault();
  captivePortal.ToDefault();
  backend.ToDefault();
  serialInput.ToDefault();
  otaUpdate.ToDefault();
}

bool RootConfig::FromFlatbuffers(const Serialization::Configuration::HubConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  if (!rf.FromFlatbuffers(config->rf())) {
    ESP_LOGE(TAG, "Unable to load rf config");
    return false;
  }

  if (!wifi.FromFlatbuffers(config->wifi())) {
    ESP_LOGE(TAG, "Unable to load wifi config");
    return false;
  }

  if (!captivePortal.FromFlatbuffers(config->captive_portal())) {
    ESP_LOGE(TAG, "Unable to load captive portal config");
    return false;
  }

  if (!backend.FromFlatbuffers(config->backend())) {
    ESP_LOGE(TAG, "Unable to load backend config");
    return false;
  }

  if (!serialInput.FromFlatbuffers(config->serial_input())) {
    ESP_LOGE(TAG, "Unable to load serial input config");
    return false;
  }

  if (!otaUpdate.FromFlatbuffers(config->ota_update())) {
    ESP_LOGE(TAG, "Unable to load ota update config");
    return false;
  }

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::HubConfig> RootConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  auto rfOffset            = rf.ToFlatbuffers(builder, withSensitiveData);
  auto wifiOffset          = wifi.ToFlatbuffers(builder, withSensitiveData);
  auto captivePortalOffset = captivePortal.ToFlatbuffers(builder, withSensitiveData);
  auto backendOffset       = backend.ToFlatbuffers(builder, withSensitiveData);
  auto serialInputOffset   = serialInput.ToFlatbuffers(builder, withSensitiveData);
  auto otaUpdateOffset     = otaUpdate.ToFlatbuffers(builder, withSensitiveData);

  return Serialization::Configuration::CreateHubConfig(builder, rfOffset, wifiOffset, captivePortalOffset, backendOffset, serialInputOffset, otaUpdateOffset);
}

bool RootConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  if (!rf.FromJSON(cJSON_GetObjectItemCaseSensitive(json, "rf"))) {
    ESP_LOGE(TAG, "Unable to load rf config");
    return false;
  }

  if (!wifi.FromJSON(cJSON_GetObjectItemCaseSensitive(json, "wifi"))) {
    ESP_LOGE(TAG, "Unable to load wifi config");
    return false;
  }

  if (!captivePortal.FromJSON(cJSON_GetObjectItemCaseSensitive(json, "captivePortal"))) {
    ESP_LOGE(TAG, "Unable to load captive portal config");
    return false;
  }

  if (!backend.FromJSON(cJSON_GetObjectItemCaseSensitive(json, "backend"))) {
    ESP_LOGE(TAG, "Unable to load backend config");
    return false;
  }

  if (!serialInput.FromJSON(cJSON_GetObjectItemCaseSensitive(json, "serialInput"))) {
    ESP_LOGE(TAG, "Unable to load serial input config");
    return false;
  }

  if (!otaUpdate.FromJSON(cJSON_GetObjectItemCaseSensitive(json, "otaUpdate"))) {
    ESP_LOGE(TAG, "Unable to load ota update config");
    return false;
  }

  return true;
}

cJSON* RootConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddItemToObject(root, "rf", rf.ToJSON(withSensitiveData));
  cJSON_AddItemToObject(root, "wifi", wifi.ToJSON(withSensitiveData));
  cJSON_AddItemToObject(root, "captivePortal", captivePortal.ToJSON(withSensitiveData));
  cJSON_AddItemToObject(root, "backend", backend.ToJSON(withSensitiveData));
  cJSON_AddItemToObject(root, "serialInput", serialInput.ToJSON(withSensitiveData));
  cJSON_AddItemToObject(root, "otaUpdate", otaUpdate.ToJSON(withSensitiveData));

  return root;
}
