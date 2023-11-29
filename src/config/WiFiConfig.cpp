#include "config/WiFiConfig.h"

#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::WiFiConfig";

using namespace OpenShock::Config;

void WiFiConfig::ToDefault() {
  accessPointSSID = OPENSHOCK_FW_AP_PREFIX;
  hostname        = OPENSHOCK_FW_HOSTNAME;
  credentialsList.clear();
}

bool WiFiConfig::FromFlatbuffers(const Serialization::Configuration::WiFiConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  Internal::Utils::FromFbsStr(accessPointSSID, config->ap_ssid(), OPENSHOCK_FW_AP_PREFIX);
  Internal::Utils::FromFbsStr(hostname, config->hostname(), OPENSHOCK_FW_HOSTNAME);
  Internal::Utils::FromFbsVec(credentialsList, config->credentials());

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiConfig> WiFiConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder) const {
  std::vector<flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiCredentials>> fbsCredentialsList;
  for (auto& credentials : credentialsList) {
    fbsCredentialsList.push_back(credentials.ToFlatbuffers(builder));
  }

  return Serialization::Configuration::CreateWiFiConfig(builder, builder.CreateString(accessPointSSID), builder.CreateString(hostname), builder.CreateVector(fbsCredentialsList));
}

bool WiFiConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (!cJSON_IsObject(json)) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  const cJSON* accessPointSSIDJson = cJSON_GetObjectItemCaseSensitive(json, "accessPointSSID");
  if (accessPointSSIDJson == nullptr) {
    ESP_LOGE(TAG, "accessPointSSID is null");
    return false;
  }

  if (!cJSON_IsString(accessPointSSIDJson)) {
    ESP_LOGE(TAG, "accessPointSSID is not a string");
    return false;
  }

  accessPointSSID = accessPointSSIDJson->valuestring;

  const cJSON* hostnameJson = cJSON_GetObjectItemCaseSensitive(json, "hostname");
  if (hostnameJson == nullptr) {
    ESP_LOGE(TAG, "hostname is null");
    return false;
  }

  if (!cJSON_IsString(hostnameJson)) {
    ESP_LOGE(TAG, "hostname is not a string");
    return false;
  }

  hostname = hostnameJson->valuestring;

  const cJSON* credentialsListJson = cJSON_GetObjectItemCaseSensitive(json, "credentials");
  if (credentialsListJson == nullptr) {
    ESP_LOGE(TAG, "credentials is null");
    return false;
  }

  if (!cJSON_IsArray(credentialsListJson)) {
    ESP_LOGE(TAG, "credentials is not an array");
    return false;
  }

  credentialsList.clear();

  const cJSON* credentialsJson = nullptr;
  cJSON_ArrayForEach(credentialsJson, credentialsListJson) {
    WiFiCredentials wifiCredential;
    if (!wifiCredential.FromJSON(credentialsJson)) {
      ESP_LOGE(TAG, "Failed to parse WiFiCredential");
      return false;
    }

    credentialsList.push_back(std::move(wifiCredential));
  }

  return true;
}

cJSON* WiFiConfig::ToJSON() const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddStringToObject(root, "accessPointSSID", accessPointSSID.c_str());
  cJSON_AddStringToObject(root, "hostname", hostname.c_str());

  cJSON* credentialsListJson = cJSON_CreateArray();

  for (auto& credentials : credentialsList) {
    cJSON_AddItemToArray(credentialsListJson, credentials.ToJSON());
  }

  cJSON_AddItemToObject(root, "credentials", credentialsListJson);

  return root;
}
