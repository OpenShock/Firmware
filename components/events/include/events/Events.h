#pragma once

#include <esp_event.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(OPENSHOCK_EVENTS);

enum {
  OPENSHOCK_EVENT_ESTOP_STATE_CHANGED,           // Event for when the EStop activation state changes
  OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED,  // Event for when the gateway connection state changes
  OPENSHOCK_EVENT_WIFI_STATE_CHANGED,            // Event for when the WiFi station connectivity state changes
  OPENSHOCK_EVENT_NETWORK_UP,                    // First interface (WiFi or Ethernet) got an IP
  OPENSHOCK_EVENT_NETWORK_DOWN,                  // Last interface lost its IP
  OPENSHOCK_EVENT_NETWORK_GOT_IP,                // Preferred interface acquired an IP or changed
};

// Payload for OPENSHOCK_EVENT_NETWORK_*. `iface` holds OpenShock::NetworkManager::Interface
// as a raw uint8_t (0=None, 1=WiFi, 2=Ethernet).
typedef struct openshock_network_event {
  uint8_t iface;
} openshock_network_event_t;

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
