#pragma once

#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(OPENSHOCK_EVENTS);

enum {
  OPENSHOCK_EVENT_ESTOP_STATE_CHANGED,           // Event for when the EStop activation state changes
  OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED,  // Event for when the gateway connection state changes
  OPENSHOCK_EVENT_WIFI_STATE_CHANGED,            // Event for when the WiFi station connectivity state changes
};

// Coarse WiFi station connectivity, posted as the OPENSHOCK_EVENT_WIFI_STATE_CHANGED payload.
typedef enum {
  OPENSHOCK_WIFI_STATE_DISCONNECTED = 0,  // Not associated to an AP
  OPENSHOCK_WIFI_STATE_CONNECTING,        // Association / auth in progress
  OPENSHOCK_WIFI_STATE_CONNECTED,         // Associated (L2), no IP yet
  OPENSHOCK_WIFI_STATE_GOT_IP,            // Associated and has an IP address (L3, usable)
} OpenShockWiFiState;

#ifdef __cplusplus
}
#endif

namespace OpenShock::Events {
  bool Init();
}
