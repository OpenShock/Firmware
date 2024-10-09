#pragma once

#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(OPENSHOCK_EVENTS);

enum {
  OPENSHOCK_EVENT_GATEWAY_CONNECTED,
  OPENSHOCK_EVENT_GATEWAY_DISCONNECTED,
};

#ifdef __cplusplus
}
#endif
