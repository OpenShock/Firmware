#include "config/DnsConfig.h"

#include "config/internal/utils.h"
#include "FormatHelpers.h"
#include "Logging.h"

const char* const TAG = "Config::DnsConfig";

using namespace OpenShock::Config;

#define DNS_USE_DHCP  true
#define DNS_PRIMARY   8, 8, 8, 8  // Google Primary
#define DNS_SECONDARY 8, 8, 4, 4  // Google Secondary
#define DNS_FALLBACK  1, 1, 1, 1  // Cloudflare Primary

bool IsUnassigned(const IPAddress& ip) {
  return ip.operator std::uint32_t() == 0;
}
void LogDnsWarnings(const IPAddress& primary, const IPAddress& secondary, const IPAddress& fallback) {
  if (IsUnassigned(primary)) {
    ESP_LOGW(TAG, "Primary DNS is unassigned (0.0.0.0), device will fall back to DHCP");
  }

  if (IsUnassigned(secondary)) {
    ESP_LOGW(TAG, "Secondary DNS is unassigned (0.0.0.0), device will fall back to DHCP");
  }

  if (IsUnassigned(fallback)) {
    ESP_LOGW(TAG, "Fallback DNS is unassigned (0.0.0.0), if primary and secondary fail, device will not be able to resolve DNS");
  }
}

DnsConfig::DnsConfig() : useDhcp(DNS_USE_DHCP), primary(DNS_PRIMARY), secondary(DNS_SECONDARY), fallback(DNS_FALLBACK) { }

DnsConfig::DnsConfig(bool useDhcp, IPAddress primary, IPAddress secondary, IPAddress fallback) : useDhcp(useDhcp), primary(primary), secondary(secondary), fallback(fallback) {
  LogDnsWarnings(primary, secondary, fallback);
}

void DnsConfig::ToDefault() {
  useDhcp   = DNS_USE_DHCP;
  primary   = IPAddress(DNS_PRIMARY);
  secondary = IPAddress(DNS_SECONDARY);
  fallback  = IPAddress(DNS_FALLBACK);
}

bool DnsConfig::FromFlatbuffers(const Serialization::Configuration::DnsConfig* config) {
  if (config == nullptr) {
    ESP_LOGE(TAG, "config is null");
    return false;
  }

  useDhcp = config->use_dhcp();

  if (!Internal::Utils::FromFbsIPAddress(primary, config->primary(), IPAddress(DNS_PRIMARY))) {
    ESP_LOGE(TAG, "failed to parse primary");
    return false;
  }

  if (!Internal::Utils::FromFbsIPAddress(secondary, config->secondary(), IPAddress(DNS_SECONDARY))) {
    ESP_LOGE(TAG, "failed to parse secondary");
    return false;
  }

  if (!Internal::Utils::FromFbsIPAddress(fallback, config->fallback(), IPAddress(DNS_FALLBACK))) {
    ESP_LOGE(TAG, "failed to parse fallback");
    return false;
  }

  LogDnsWarnings(primary, secondary, fallback);

  return true;
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::DnsConfig> DnsConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const {
  flatbuffers::Offset<flatbuffers::String> primaryStr, secondaryStr, fallbackStr;

  char address[IPV4ADDR_FMT_LEN + 1] = {0};

  sniprintf(address, IPV4ADDR_FMT_LEN, IPV4ADDR_FMT, IPV4ADDR_ARG(primary));
  primaryStr = builder.CreateString(address);

  sniprintf(address, IPV4ADDR_FMT_LEN, IPV4ADDR_FMT, IPV4ADDR_ARG(secondary));
  secondaryStr = builder.CreateString(address);

  sniprintf(address, IPV4ADDR_FMT_LEN, IPV4ADDR_FMT, IPV4ADDR_ARG(fallback));
  fallbackStr = builder.CreateString(address);

  return Serialization::Configuration::CreateDnsConfig(builder, useDhcp, primaryStr, secondaryStr, fallbackStr);
}

bool DnsConfig::FromJSON(const cJSON* json) {
  if (json == nullptr) {
    ESP_LOGE(TAG, "json is null");
    return false;
  }

  if (cJSON_IsObject(json) == 0) {
    ESP_LOGE(TAG, "json is not an object");
    return false;
  }

  if (!Internal::Utils::FromJsonBool(useDhcp, json, "use_dhcp", DNS_USE_DHCP)) {
    ESP_LOGE(TAG, "failed to parse autoConfigDNS");
    return false;
  }

  if (!Internal::Utils::FromJsonIPAddress(primary, json, "primary", IPAddress(DNS_PRIMARY))) {
    ESP_LOGE(TAG, "failed to parse primary");
    return false;
  }

  if (!Internal::Utils::FromJsonIPAddress(secondary, json, "secondary", IPAddress(DNS_SECONDARY))) {
    ESP_LOGE(TAG, "failed to parse secondary");
    return false;
  }

  if (!Internal::Utils::FromJsonIPAddress(fallback, json, "fallback", IPAddress(DNS_FALLBACK))) {
    ESP_LOGE(TAG, "failed to parse fallback");
    return false;
  }

  LogDnsWarnings(primary, secondary, fallback);

  return true;
}

cJSON* DnsConfig::ToJSON(bool withSensitiveData) const {
  cJSON* root = cJSON_CreateObject();

  cJSON_AddBoolToObject(root, "use_dhcp", useDhcp);

  char address[IPV4ADDR_FMT_LEN + 1] = {0};

  snprintf(address, IPV4ADDR_FMT_LEN, IPV4ADDR_FMT, IPV4ADDR_ARG(primary));
  cJSON_AddStringToObject(root, "primary", address);

  snprintf(address, IPV4ADDR_FMT_LEN, IPV4ADDR_FMT, IPV4ADDR_ARG(secondary));
  cJSON_AddStringToObject(root, "secondary", address);

  snprintf(address, IPV4ADDR_FMT_LEN, IPV4ADDR_FMT, IPV4ADDR_ARG(fallback));
  cJSON_AddStringToObject(root, "fallback", address);

  return root;
}
