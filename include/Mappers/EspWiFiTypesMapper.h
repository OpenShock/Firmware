#pragma once

#include "fbs/WifiAuthMode_generated.h"

#include <esp_wifi_types.h>

namespace OpenShock::Mappers {
  constexpr WifiAuthMode GetWiFiAuthModeEnum(wifi_auth_mode_t authMode) {
    switch (authMode) {
      case WIFI_AUTH_OPEN:
        return OpenShock::WifiAuthMode::Open;
      case WIFI_AUTH_WEP:
        return OpenShock::WifiAuthMode::WEP;
      case WIFI_AUTH_WPA_PSK:
        return OpenShock::WifiAuthMode::WPA_PSK;
      case WIFI_AUTH_WPA2_PSK:
        return OpenShock::WifiAuthMode::WPA2_PSK;
      case WIFI_AUTH_WPA_WPA2_PSK:
        return OpenShock::WifiAuthMode::WPA_WPA2_PSK;
      case WIFI_AUTH_WPA2_ENTERPRISE:
        return OpenShock::WifiAuthMode::WPA2_ENTERPRISE;
      case WIFI_AUTH_WPA3_PSK:
        return OpenShock::WifiAuthMode::WPA3_PSK;
      case WIFI_AUTH_WPA2_WPA3_PSK:
        return OpenShock::WifiAuthMode::WPA2_WPA3_PSK;
      case WIFI_AUTH_WAPI_PSK:
        return OpenShock::WifiAuthMode::WAPI_PSK;
      default:
        return OpenShock::WifiAuthMode::UNKNOWN;
    }
  }

  /*
   * @brief Maps a WiFi auth mode to a human-readable string.
   * @param authMode The auth mode to map.
   * @return A human-readable string representing the auth mode, or nullptr if the auth mode is invalid.
   */
  inline const char* const GetWiFiAuthModeString(wifi_auth_mode_t authMode) {
    return OpenShock::EnumNameWifiAuthMode(GetWiFiAuthModeEnum(authMode));
  }
}  // namespace OpenShock::Mappers
