#include "EStopManager.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "Logging.h"
#include "Time.h"
#include "VisualStateManager.h"

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

const char* const TAG = "EStopManager";

using namespace OpenShock;

const std::uint32_t k_estopHoldToClearTime = 5000;
const std::uint32_t k_estopDebounceTime    = 100;

static TaskHandle_t s_estopEventHandlerTask;
static QueueHandle_t s_estopEventQueue;

static EStopManager::EStopStatus s_estopStatus = EStopManager::EStopStatus::ALL_CLEAR;
static std::int64_t s_estopActivatedAt         = 0;

static gpio_num_t s_estopPin;

struct EstopEventQueueMessage {
  bool pressed              : 1;
  bool deactivatesAtChanged : 1;
  std::int64_t deactivatesAt;
};

// This high-priority task is usually idling, waiting for
// messages from the EStop interrupt or it's hold timer
void _estopEventHandler(void* pvParameters) {
  std::int64_t deactivatesAt = 0;
  for (;;) {
    // Wait indefinitely for a message from the EStop interrupt routine
    TickType_t waitTime = portMAX_DELAY;

    // If the EStop is being deactivated, wait for the hold timer to trigger
    if (deactivatesAt != 0) {
      std::int64_t now = OpenShock::millis();
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
        ESP_LOGI(TAG, "EStop pressed");
      } else {
        ESP_LOGI(TAG, "EStop released");
      }

      if (message.deactivatesAtChanged) {
        ESP_LOGI(TAG, "EStop deactivation time changed");
      }

      OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      OpenShock::CommandHandler::SetKeepAlivePaused(EStopManager::IsEStopped());
    } else if (deactivatesAt != 0 && OpenShock::millis() >= deactivatesAt) {  // If we didn't get a message, the time probably expired, check if the estop is pending deactivation and if we have reached that time
      // Reset the deactivation time
      deactivatesAt = 0;

      // If the button is held for the specified time, clear the EStop
      s_estopStatus = EStopManager::EStopStatus::ESTOPPED_CLEARED;
      ESP_LOGI(TAG, "EStop cleared, awaiting release");
    }
  }
}

// Interrupt should only be a dumb sender of the GPIO change, additionally triggering if needed
// Clearing and debouncing is handled by the task.
void _estopEdgeInterrupt(void* arg) {
  std::int64_t now = OpenShock::millis();

  // TODO: Allow active HIGH EStops?
  bool pressed = gpio_get_level(s_estopPin) == 0;

  bool deactivatesAtChanged  = false;
  std::int64_t deactivatesAt = 0;

  // Set the internal state here quickly, we'll do more complex notifications outside of the ISR.
  switch (s_estopStatus) {
    case EStopManager::EStopStatus::ALL_CLEAR:
      if (pressed) {
        s_estopStatus      = EStopManager::EStopStatus::ESTOPPED_AND_HELD;
        s_estopActivatedAt = now;
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_AND_HELD:
      // Hypothetical edge case with this debounce would require the user to tap it again to exit AND_HELD state,
      // However, I can't create the behavior in testing*, so a hypothetical annoyance is an acceptable trade-off.
      // *(Likely due to extra release events being sent into the queue on each press)
      if (!pressed && (now - s_estopActivatedAt >= k_estopDebounceTime)) {
        // User has released the button, now we can trust them holding to clear it.
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED;
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED:
      if (pressed && (now - s_estopActivatedAt >= k_estopDebounceTime)) {
        deactivatesAtChanged = true;
        deactivatesAt        = now + k_estopHoldToClearTime;
      } else if (!pressed) {
        deactivatesAtChanged = true;
        deactivatesAt        = 0;
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_CLEARED:
      // If the button is released, report as ALL_CLEAR
      if (!pressed) {
        s_estopStatus = EStopManager::EStopStatus::ALL_CLEAR;
      }
      break;

    default:
      break;
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

void EStopManager::Init() {
#ifdef OPENSHOCK_ESTOP_PIN
  s_estopPin = OPENSHOCK_ESTOP_PIN;

  ESP_LOGI(TAG, "Initializing on pin %hhi", static_cast<std::int8_t>(s_estopPin));

  gpio_config_t io_conf;
  io_conf.pin_bit_mask = 1ULL << s_estopPin;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
  io_conf.mode         = GPIO_MODE_INPUT;
  io_conf.intr_type    = GPIO_INTR_ANYEDGE;
  gpio_config(&io_conf);

  // TODO?: Should we maybe use statically allocated queues and timers? See CreateStatic for both.
  s_estopEventQueue = xQueueCreate(8, sizeof(EstopEventQueueMessage));

  esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  // ESP_ERR_INVALID_STATE is fine, it just means the ISR service is already installed
    ESP_PANIC(TAG, "Failed to install EStop ISR service");
  }

  err = gpio_isr_handler_add(s_estopPin, _estopEdgeInterrupt, nullptr);
  if (err != ESP_OK) {
    ESP_PANIC(TAG, "Failed to add EStop ISR handler");
  }

  if (xTaskCreate(_estopEventHandler, TAG, 4096, nullptr, 5, &s_estopEventHandlerTask) != pdPASS) {
    ESP_PANIC(TAG, "Failed to create EStop event handler task");
  }

#else
  ESP_LOGI(TAG, "EStopManager disabled, no pin defined");
#endif
}

bool EStopManager::IsEStopped() {
  return s_estopStatus != EStopManager::EStopStatus::ALL_CLEAR;
}

std::int64_t EStopManager::LastEStopped() {
  return s_estopActivatedAt;
}
