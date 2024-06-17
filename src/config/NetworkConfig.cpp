#include "config/NetworkConfig.h"

#include "config/internal/utils.h"
#include "Logging.h"

const char* const TAG = "Config::NetworkConfig";

using namespace OpenShock::Config;

#define DNS_PRIMARY   1, 1, 1, 1
#define DNS_SECONDARY 8, 8, 8, 8
#define DNS_FALLBACK  9, 9, 9, 9

NetworkConfig::NetworkConfig() : primaryDNS(DNS_PRIMARY), secondaryDNS(DNS_SECONDARY), fallbackDNS(DNS_FALLBACK) { }

NetworkConfig::NetworkConfig(IPAddress primaryDNS, IPAddress secondaryDNS, IPAddress fallbackDNS) : primaryDNS(primaryDNS), secondaryDNS(secondaryDNS), fallbackDNS(fallbackDNS) { }

void NetworkConfig::ToDefault() {
  primaryDNS   = IPAddress(DNS_PRIMARY);
  secondaryDNS = IPAddress(DNS_SECONDARY);
  fallbackDNS  = IPAddress(DNS_FALLBACK);
}

bool NetworkConfig::FromFlatbuffers(const Serialization::Configuration::NetworkConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  if (config->dns_primary() == nullptr) {
    ESP_LOGE(TAG, "dns_primary is null");
    return false;
  }

  if (config->dns_secondary() == nullptr) {
    ESP_LOGE(TAG, "dns_secondary is null");
    return false;
  }

  if (config->dns_fallback() == nullptr) {
    ESP_LOGE(TAG, "dns_fallback is null");
    return false;
  }

  if (!Internal::Utils::FromFbsIPAddress(primaryDNS, config->dns_primary(), IPAddress(DNS_PRIMARY))) {
    ESP_LOGE(TAG, "failed to parse primaryDNS");
    return false;
  }

  if (!Internal::Utils::FromFbsIPAddress(secondaryDNS, config->dns_secondary(), IPAddress(DNS_SECONDARY))) {
    ESP_LOGE(TAG, "failed to parse secondaryDNS");
    return false;
  }

  if (!Internal::Utils::FromFbsIPAddress(fallbackDNS, config->dns_fallback(), IPAddress(DNS_FALLBACK))) {
    ESP_LOGE(TAG, "failed to parse fallbackDNS");
    return false;
  }

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::NetworkConfig> NetworkConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  char primaryDNSStr[16];
  snprintf(primaryDNSStr, sizeof(primaryDNSStr), "%d.%d.%d.%d", primaryDNS[0], primaryDNS[1], primaryDNS[2], primaryDNS[3]);

  char secondaryDNSStr[16];
  snprintf(secondaryDNSStr, sizeof(secondaryDNSStr), "%d.%d.%d.%d", secondaryDNS[0], secondaryDNS[1], secondaryDNS[2], secondaryDNS[3]);

  char fallbackDNSStr[16];
  snprintf(fallbackDNSStr, sizeof(fallbackDNSStr), "%d.%d.%d.%d", fallbackDNS[0], fallbackDNS[1], fallbackDNS[2], fallbackDNS[3]);

  return Serialization::Configuration::CreateNetworkConfig(builder, builder.CreateString(primaryDNSStr), builder.CreateString(secondaryDNSStr), builder.CreateString(fallbackDNSStr));
}

bool NetworkConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  if (!Internal::Utils::FromJsonIPAddress(primaryDNS, json, "dns_primary", IPAddress(DNS_PRIMARY))) {
    ESP_LOGE(TAG, "failed to parse primaryDNS");
    return false;
  }

  if (!Internal::Utils::FromJsonIPAddress(secondaryDNS, json, "dns_secondary", IPAddress(DNS_SECONDARY))) {
    ESP_LOGE(TAG, "failed to parse secondaryDNS");
    return false;
  }

  if (!Internal::Utils::FromJsonIPAddress(fallbackDNS, json, "dns_fallback", IPAddress(DNS_FALLBACK))) {
    ESP_LOGE(TAG, "failed to parse fallbackDNS");
    return false;
  }

  return true;
}

cJSON* NetworkConfig::ToJSON(bool withSensitiveData) const {
  char primaryDNSStr[16];
  snprintf(primaryDNSStr, sizeof(primaryDNSStr), "%d.%d.%d.%d", primaryDNS[0], primaryDNS[1], primaryDNS[2], primaryDNS[3]);

  char secondaryDNSStr[16];
  snprintf(secondaryDNSStr, sizeof(secondaryDNSStr), "%d.%d.%d.%d", secondaryDNS[0], secondaryDNS[1], secondaryDNS[2], secondaryDNS[3]);

  char fallbackDNSStr[16];
  snprintf(fallbackDNSStr, sizeof(fallbackDNSStr), "%d.%d.%d.%d", fallbackDNS[0], fallbackDNS[1], fallbackDNS[2], fallbackDNS[3]);

  cJSON* root = cJSON_CreateObject();

  cJSON_AddStringToObject(root, "dns_primary", primaryDNSStr);
  cJSON_AddStringToObject(root, "dns_secondary", secondaryDNSStr);
  cJSON_AddStringToObject(root, "dns_fallback", fallbackDNSStr);

  return root;
}
