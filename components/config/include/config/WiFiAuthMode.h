#pragma once

#include "serialization/_fbs/WifiAuthMode_generated.h"

#include <esp_wifi_types.h>

namespace OpenShock::Config {
  // The one mapping between ESP-IDF's wifi_auth_mode_t and the FlatBuffers WifiAuthMode used by the stored config
  // and the local (captive portal) protocol. Modes without a FlatBuffers counterpart map to UNKNOWN / WIFI_AUTH_MAX.
  constexpr Serialization::Types::WifiAuthMode ToFbsAuthMode(wifi_auth_mode_t mode)
  {
    using FbsAuthMode = Serialization::Types::WifiAuthMode;
    switch (mode) {
      case WIFI_AUTH_OPEN:
        return FbsAuthMode::Open;
      case WIFI_AUTH_WEP:
        return FbsAuthMode::WEP;
      case WIFI_AUTH_WPA_PSK:
        return FbsAuthMode::WPA_PSK;
      case WIFI_AUTH_WPA2_PSK:
        return FbsAuthMode::WPA2_PSK;
      case WIFI_AUTH_WPA_WPA2_PSK:
        return FbsAuthMode::WPA_WPA2_PSK;
      case WIFI_AUTH_WPA2_ENTERPRISE:
        return FbsAuthMode::WPA2_ENTERPRISE;
      case WIFI_AUTH_WPA3_PSK:
        return FbsAuthMode::WPA3_PSK;
      case WIFI_AUTH_WPA2_WPA3_PSK:
        return FbsAuthMode::WPA2_WPA3_PSK;
      case WIFI_AUTH_WAPI_PSK:
        return FbsAuthMode::WAPI_PSK;
      default:
        return FbsAuthMode::UNKNOWN;
    }
  }

  constexpr wifi_auth_mode_t FromFbsAuthMode(Serialization::Types::WifiAuthMode mode)
  {
    using FbsAuthMode = Serialization::Types::WifiAuthMode;
    switch (mode) {
      case FbsAuthMode::Open:
        return WIFI_AUTH_OPEN;
      case FbsAuthMode::WEP:
        return WIFI_AUTH_WEP;
      case FbsAuthMode::WPA_PSK:
        return WIFI_AUTH_WPA_PSK;
      case FbsAuthMode::WPA2_PSK:
        return WIFI_AUTH_WPA2_PSK;
      case FbsAuthMode::WPA_WPA2_PSK:
        return WIFI_AUTH_WPA_WPA2_PSK;
      case FbsAuthMode::WPA2_ENTERPRISE:
        return WIFI_AUTH_WPA2_ENTERPRISE;
      case FbsAuthMode::WPA3_PSK:
        return WIFI_AUTH_WPA3_PSK;
      case FbsAuthMode::WPA2_WPA3_PSK:
        return WIFI_AUTH_WPA2_WPA3_PSK;
      case FbsAuthMode::WAPI_PSK:
        return WIFI_AUTH_WAPI_PSK;
      default:
        return WIFI_AUTH_MAX;
    }
  }
}  // namespace OpenShock::Config
