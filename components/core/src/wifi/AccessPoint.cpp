#include <freertos/FreeRTOS.h>

#include "wifi/AccessPoint.h"

#include "FormatHelpers.h"
#include "wifi/WiFiConstants.h"

#include <esp_mac.h>

#include <cstdio>

// The prefix gets whatever the MAC leaves of an SSID
static_assert(sizeof(CONFIG_OPENSHOCK_FW_AP_PREFIX) - 1 + OpenShock::WiFiConstants::kMacStringLength <= OpenShock::WiFiConstants::kSsidMaxLength, "CONFIG_OPENSHOCK_FW_AP_PREFIX must leave room for the MAC in the SSID");

bool OpenShock::AccessPoint::IsDefaultSSID(std::string_view configuredSsid)
{
  return configuredSsid == CONFIG_OPENSHOCK_FW_AP_PREFIX;
}

OpenShock::AccessPoint::Protection OpenShock::AccessPoint::ProtectionOf(std::string_view password)
{
  if (password.empty()) {
    return Protection::Open;
  }
  if (password == CONFIG_OPENSHOCK_FW_AP_PASSWORD) {
    return Protection::DefaultPassword;
  }
  return Protection::CustomPassword;
}

std::string OpenShock::AccessPoint::DefaultSSID()
{
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  char ssid[WiFiConstants::kSsidMaxLength + 1];
  snprintf(ssid, sizeof(ssid), "%s" BSSID_FMT, CONFIG_OPENSHOCK_FW_AP_PREFIX, BSSID_ARG(mac));
  return ssid;
}

std::string OpenShock::AccessPoint::BroadcastSSID(std::string_view configuredSsid)
{
  return IsDefaultSSID(configuredSsid) ? DefaultSSID() : std::string(configuredSsid);
}
