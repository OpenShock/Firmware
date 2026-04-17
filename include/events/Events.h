#pragma once

#include <esp_event.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(OPENSHOCK_EVENTS);

enum {
  OPENSHOCK_EVENT_ESTOP_STATE_CHANGED,           // EStop activation state changes
  OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED,  // Gateway WS connection state changes
  OPENSHOCK_EVENT_NETWORK_UP,                    // First interface reached "has IP"
  OPENSHOCK_EVENT_NETWORK_DOWN,                  // Last interface lost "has IP"
  OPENSHOCK_EVENT_NETWORK_GOT_IP,                // Preferred interface acquired an IP or changed
};

// Payload for OPENSHOCK_EVENT_NETWORK_*. `iface` holds OpenShock::NetworkManager::Interface
// as a raw uint8_t (0=None, 1=WiFi, 2=Ethernet).
typedef struct openshock_network_event {
  uint8_t iface;
} openshock_network_event_t;

#ifdef __cplusplus
}
#endif

namespace OpenShock::Events {
  bool Init();
}
