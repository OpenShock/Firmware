#include <freertos/FreeRTOS.h>

#include "EStopManager.h"

const char* const TAG = "EStopManager";

#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "events/Events.h"
#include "Logging.h"
#include "Time.h"
#include "util/TaskUtils.h"
#include "VisualStateManager.h"

#include <driver/gpio.h>
#include <freertos/queue.h>
#include <freertos/timers.h>

using namespace OpenShock;

const uint32_t k_estopHoldToClearTime = 5000;
const uint32_t k_estopDebounceTime    = 100;

static TaskHandle_t s_estopEventHandlerTask;
static QueueHandle_t s_estopEventQueue;

static EStopState s_estopState = EStopState::Idle;
static bool s_lastState            = false;
static int64_t s_lastStateChange   = 0;
static int64_t s_estopActivatedAt  = 0;

static gpio_num_t s_estopPin = GPIO_NUM_NC;

struct EstopEventQueueMessage {
  bool pressed              : 1;
  bool deactivatesAtChanged : 1;
  int64_t deactivatesAt;
};

// This high-priority task is usually idling, waiting for
// messages from the EStop interrupt or it's hold timer
void _estopEventHandler(void* pvParameters) {
  int64_t deactivatesAt = 0;
  for (;;) {
    // Wait indefinitely for a message from the EStop interrupt routine
    TickType_t waitTime = portMAX_DELAY;

    // If the EStop is being deactivated, wait for the hold timer to trigger
    if (deactivatesAt != 0) {
      int64_t now = OpenShock::millis();
      if (now >= deactivatesAt) {
        waitTime = 0;
      } else {
        waitTime = pdMS_TO_TICKS(deactivatesAt - OpenShock::millis());
      }
    }

    // Wait for a message from the EStop interrupt routine
    EstopEventQueueMessage message;
    if (xQueueReceive(s_estopEventQueue, &message, waitTime) == pdTRUE) {
      if (message.pressed) {
        OS_LOGI(TAG, "EStop pressed");
      } else {
        OS_LOGI(TAG, "EStop released");
      }

      if (message.deactivatesAtChanged) {
        OS_LOGI(TAG, "EStop deactivation time changed");
        deactivatesAt = message.deactivatesAt;
      }

      ESP_ERROR_CHECK(esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_ESTOP_STATE_CHANGED, &s_estopState, sizeof(s_estopState), portMAX_DELAY));

      OpenShock::CommandHandler::SetKeepAlivePaused(EStopManager::IsEStopped());
    } else if (deactivatesAt != 0 && OpenShock::millis() >= deactivatesAt) {  // If we didn't get a message, the time probably expired, check if the estop is pending deactivation and if we have reached that time
      // Reset the deactivation time
      deactivatesAt = 0;

      // If the button is held for the specified time, clear the EStop
      s_estopState = EStopState::AwaitingRelease;

      ESP_ERROR_CHECK(esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_ESTOP_STATE_CHANGED, &s_estopState, sizeof(s_estopState), portMAX_DELAY));

      OS_LOGI(TAG, "EStop cleared, awaiting release");
    }
  }
}

// Interrupt should only be a dumb sender of the GPIO change, additionally triggering if needed
// Clearing and debouncing is handled by the task.
void _estopEdgeInterrupt(void* arg) {
  int64_t now = OpenShock::millis();

  // Debounce the EStop
  bool debounce = now - s_lastStateChange < k_estopDebounceTime;
  if (debounce) {
    return;
  }

  // TODO: Allow active HIGH EStops?
  bool pressed = gpio_get_level(s_estopPin) == 0;

  // If the state hasn't changed, ignore it (debounce will skip state changes)
  if (pressed == s_lastState) {
    return;
  }
  s_lastState       = pressed;
  s_lastStateChange = now;

  bool deactivatesAtChanged = false;
  int64_t deactivatesAt     = 0;

  if (pressed && s_estopState == EStopState::Idle) {
    s_estopState = EStopState::Active;
    s_estopActivatedAt = now;
  } else if (pressed && s_estopState == EStopState::Active) {
    deactivatesAtChanged = true;
    deactivatesAt        = now + k_estopHoldToClearTime;
  } else if (!pressed && s_estopState == EStopState::AwaitingRelease) {
    s_estopState = EStopState::Idle;
  } else if (!pressed && s_estopState == EStopState::Active) {
    deactivatesAtChanged = true;
    deactivatesAt        = 0;
  } else {
    return;
  }

  BaseType_t higherPriorityTaskWoken = pdFALSE;
  EstopEventQueueMessage message     = {
        .pressed              = pressed,
        .deactivatesAtChanged = deactivatesAtChanged,
        .deactivatesAt        = deactivatesAt,
  };

  xQueueSendToBackFromISR(s_estopEventQueue, &message, &higherPriorityTaskWoken);  // TODO: Check if queue is full?

  if (higherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

bool EStopManager::Init() {
  bool enabled = false;
  if (!OpenShock::Config::GetEStopEnabled(enabled)) {
    OS_LOGE(TAG, "Failed to get EStop enabled from config");
    return false;
  }
  if (!enabled) {
    OS_LOGI(TAG, "EStop disabled in config");
    return true;  // TODO: If we never initialize the EStop, how do we do this later for enabling/disabling?
  }

  gpio_num_t pin = GPIO_NUM_NC;
  if (!OpenShock::Config::GetEStopGpioPin(pin)) {
    OS_LOGE(TAG, "Failed to get EStop pin from config");
    return false;
  }

  OS_LOGI(TAG, "Initializing on pin %hhi", static_cast<int8_t>(pin));

  // TODO?: Should we maybe use statically allocated queues and timers? See CreateStatic for both.
  s_estopEventQueue = xQueueCreate(8, sizeof(EstopEventQueueMessage));

  if (!EStopManager::SetEStopPin(pin)) {
    OS_LOGE(TAG, "Failed to set EStop pin");
    return false;
  }

  if (TaskUtils::TaskCreateUniversal(_estopEventHandler, TAG, 4096, nullptr, 5, &s_estopEventHandlerTask, 1) != pdPASS) {
    OS_LOGE(TAG, "Failed to create EStop event handler task");
    return false;
  }

  return true;
}

bool EStopManager::SetEStopEnabled(bool enabled) {
  // TODO: Implement

  return true;
}

bool EStopManager::SetEStopPin(gpio_num_t pin) {
  if (!OpenShock::IsValidInputPin(pin)) {
    OS_LOGE(TAG, "Invalid EStop pin: %hhi", static_cast<int8_t>(pin));
    return false;
  }

  esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  // ESP_ERR_INVALID_STATE is fine, it just means the ISR service is already installed
    OS_LOGE(TAG, "Failed to install EStop ISR service");
    return false;
  }

  // Configure the new pin
  gpio_config_t io_conf;
  io_conf.pin_bit_mask = 1ULL << pin;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
  io_conf.mode         = GPIO_MODE_INPUT;
  io_conf.intr_type    = GPIO_INTR_ANYEDGE;
  if (gpio_config(&io_conf) != ESP_OK) {
    OS_LOGE(TAG, "Failed to configure EStop pin");
    return false;
  }

  // Add the new interrupt
  err = gpio_isr_handler_add(pin, _estopEdgeInterrupt, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to add EStop ISR handler");
    return false;
  }

  gpio_num_t oldPin = s_estopPin;

  // Set the new pin
  s_estopPin = pin;

  if (oldPin != GPIO_NUM_NC) {
    // Remove the old interrupt
    esp_err_t err = gpio_isr_handler_remove(oldPin);
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to remove old EStop ISR handler");
      return false;
    }

    // Reset the old pin
    err = gpio_reset_pin(oldPin);
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to reset old EStop pin");
      return false;
    }
  }

  return true;
}

bool EStopManager::IsEStopped() {
  return s_estopState != EStopState::Idle;
}

int64_t EStopManager::LastEStopped() {
  return s_estopActivatedAt;
}
