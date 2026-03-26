#include <freertos/FreeRTOS.h>

#include "events/Events.h"

#include "Logging.h"

const char* const TAG = "Events";

ESP_EVENT_DEFINE_BASE(OPENSHOCK_EVENTS);

using namespace OpenShock;

bool Events::Init()
{
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to create default event loop: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}
