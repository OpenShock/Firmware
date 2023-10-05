#pragma once

#include <esp_wifi_types.h>

namespace OpenShock::Mappers {
  /*
    * @brief Maps a WiFi auth mode to a human-readable string.
    * @param authMode The auth mode to map.
    * @return A human-readable string representing the auth mode, or nullptr if the auth mode is invalid.
  */
  const char* GetWiFiAuthModeName(wifi_auth_mode_t authMode);
}
