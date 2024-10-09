#pragma once

#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(OPENSHOCK_EVENTS);

enum {
  // Event for when the gateway connection state changes
  OPENSHOCK_EVENT_GATEWAY_STATE_CHANGED,
};

#ifdef __cplusplus
}
#endif

namespace OpenShock::Events {
  bool Init();
}
