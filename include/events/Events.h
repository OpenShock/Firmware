#pragma once

#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(OPENSHOCK_EVENTS);

enum {
  OPENSHOCK_EVENT_ESTOP_STATE_CHANGED,           // Event for when the EStop activation state changes
  OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED,  // Event for when the gateway connection state changes
};

#ifdef __cplusplus
}
#endif

namespace OpenShock::Events {
  bool Init();
}
