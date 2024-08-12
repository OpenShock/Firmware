#include "EStopManager.h"

#define OPENSHOCK_ESTOP_PIN GPIO_NUM_0

#include "CommandHandler.h"
#include "config/Config.h"
#include "Logging.h"
#include "Time.h"
#include "VisualStateManager.h"

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <driver/gpio.h>

const char* const TAG = "EStopManager";

using namespace OpenShock;

const std::uint32_t k_estopHoldToClearTime = 5000;

static TaskHandle_t s_estopEventHandlerTask;
static QueueHandle_t s_estopEventQueue;

static bool s_estopActive              = false;
static std::int64_t s_estopActivatedAt = 0;

static gpio_num_t s_estopPin;

const std::uint8_t k_estopEventQueueMessageFlagPressed      = 1 << 0;
const std::uint8_t k_estopEventQueueMessageFlagActivated    = 1 << 1;
const std::uint8_t k_estopEventQueueMessageFlagDeactivating = 1 << 2;

struct EstopEventQueueMessage {
  std::uint8_t flags;
  std::int64_t timestamp;
};

void _estopEventHandler(void* pvParameters) {
  std::int64_t deactivatesAt = 0;
  for (;;) {
    TickType_t waitTime = portMAX_DELAY;
    if (deactivatesAt > 0) {
      waitTime = pdMS_TO_TICKS(deactivatesAt);
    }

    EstopEventQueueMessage message;
    if (xQueueReceive(s_estopEventQueue, &message, waitTime) == pdTRUE) {
      bool pressed = (message.flags & k_estopEventQueueMessageFlagPressed) != 0;
      bool activated = (message.flags & k_estopEventQueueMessageFlagActivated) != 0;
      bool deactivating = (message.flags & k_estopEventQueueMessageFlagDeactivating) != 0;

      if (pressed) {
        ESP_LOGI(TAG, "EStop button pressed");
      } else {
        ESP_LOGI(TAG, "EStop button released");
      }

      if (activated) {
        ESP_LOGI(TAG, "EStop activated");
      }

      if (activated) {
        OpenShock::VisualStateManager::SetEmergencyStop(EStopManager::EStopStatus::ESTOPPED);
        OpenShock::CommandHandler::SetKeepAlivePaused(true);
      } else if (deactivating) {
        OpenShock::VisualStateManager::SetEmergencyStop(EStopManager::EStopStatus::ESTOPPED_CLEARED);
      }

      if (deactivating) {
        deactivatesAt = message.timestamp + k_estopHoldToClearTime;
      } else {
        deactivatesAt = 0;
      }
    }

    if (deactivatesAt > 0 && OpenShock::millis() >= deactivatesAt) {
      s_estopActive = false;
      deactivatesAt = 0;

      ESP_LOGI(TAG, "EStop cleared");

      OpenShock::VisualStateManager::SetEmergencyStop(EStopManager::EStopStatus::ALL_CLEAR);
      OpenShock::CommandHandler::SetKeepAlivePaused(false);
    }
  }
}

void _estopLevelChanged(void* arg) {
  std::int64_t now = OpenShock::millis();

  // TODO?: Allow active HIGH EStops?
  bool pressed = gpio_get_level(s_estopPin) == 0;
  bool active = s_estopActive;
  bool activated = !active && pressed;
  bool deactivating = active && !pressed;

  // Instantly activate the EStop if it's pressed, rest of the logic will be handled in the task we notify
  if (activated) {
    s_estopActive = true;
    s_estopActivatedAt = now;
  }

  BaseType_t higherPriorityTaskWoken = pdFALSE;

  std::uint8_t flags = 0;

  if (pressed) {
    flags |= k_estopEventQueueMessageFlagPressed;
  }

  if (activated) {
    flags |= k_estopEventQueueMessageFlagActivated;
  }

  if (deactivating) {
    flags |= k_estopEventQueueMessageFlagDeactivating;
  }

  EstopEventQueueMessage message = {
    .flags = flags,
    .timestamp = now,
  };

  xQueueSendToBackFromISR(s_estopEventQueue, &message, &higherPriorityTaskWoken);

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

  gpio_install_isr_service(0); // TODO: This might need error checking

  if (gpio_isr_handler_add(s_estopPin, _estopLevelChanged, nullptr) != ESP_OK) {
    ESP_PANIC(TAG, "Failed to add EStop ISR handler");
  }

  if (xTaskCreate(_estopEventHandler, TAG, 2048, nullptr, 5, &s_estopEventHandlerTask) != pdPASS) {
    ESP_PANIC(TAG, "Failed to create EStop event handler task");
  }

  s_estopEventQueue = xQueueCreate(8, sizeof(EstopEventQueueMessage));

#else
  (void)updateIntervalMs;

  ESP_LOGI(TAG, "EStopManager disabled, no pin defined");
#endif
}

bool EStopManager::IsEStopped() {
  return s_estopActive;
}

std::int64_t EStopManager::LastEStopped() {
  return s_estopActivatedAt;
}
