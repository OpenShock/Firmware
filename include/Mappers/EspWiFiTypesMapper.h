#pragma once

#include "WiFiAuthMode.h"

#include <esp_wifi_types.h>

namespace OpenShock::Mappers {
  constexpr OpenShock::WiFiAuthMode GetWiFiAuthModeEnum(wifi_auth_mode_t authMode) {
    switch (authMode) {
      case WIFI_AUTH_OPEN:
        return OpenShock::WiFiAuthMode::Open;
      case WIFI_AUTH_WEP:
        return OpenShock::WiFiAuthMode::WEP;
      case WIFI_AUTH_WPA_PSK:
        return OpenShock::WiFiAuthMode::WPA_PSK;
      case WIFI_AUTH_WPA2_PSK:
        return OpenShock::WiFiAuthMode::WPA2_PSK;
      case WIFI_AUTH_WPA_WPA2_PSK:
        return OpenShock::WiFiAuthMode::WPA_WPA2_PSK;
      case WIFI_AUTH_WPA2_ENTERPRISE:
        return OpenShock::WiFiAuthMode::WPA2_ENTERPRISE;
      case WIFI_AUTH_WPA3_PSK:
        return OpenShock::WiFiAuthMode::WPA3_PSK;
      case WIFI_AUTH_WPA2_WPA3_PSK:
        return OpenShock::WiFiAuthMode::WPA2_WPA3_PSK;
      case WIFI_AUTH_WAPI_PSK:
        return OpenShock::WiFiAuthMode::WAPI_PSK;
      default:
        return OpenShock::WiFiAuthMode::UNKNOWN;
    }
  }

  /*
   * @brief Maps a WiFi auth mode to a human-readable string.
   * @param authMode The auth mode to map.
   * @return A human-readable string representing the auth mode, or nullptr if the auth mode is invalid.
   */
  inline const char* const GetWiFiAuthModeString(wifi_auth_mode_t authMode) {
    return OpenShock::Serialization::Types::EnumNameWifiAuthMode(GetWiFiAuthModeEnum(authMode));
  }
}  // namespace OpenShock::Mappers
