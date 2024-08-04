#include "EStopManager.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "Logging.h"
#include "Time.h"
#include "VisualStateManager.h"

#include <Arduino.h>

const char* const TAG = "EStopManager";

using namespace OpenShock;

static EStopManager::EStopStatus s_estopStatus   = EStopManager::EStopStatus::ALL_CLEAR;
static std::uint32_t s_estopHoldToClearTime      = 5000;
static std::int64_t s_lastEStopButtonStateChange = 0;
static std::int64_t s_estoppedAt                 = 0;
static bool s_lastEStopButtonState               = true;

static std::uint8_t s_estopPin;

void _estopManagerTask(TimerHandle_t xTimer) {
  configASSERT(xTimer);

  bool buttonState = digitalRead(s_estopPin) != 0;
  if (buttonState != s_lastEStopButtonState) {
    s_lastEStopButtonStateChange = OpenShock::millis();
  }
  switch (s_estopStatus) {
    case EStopManager::EStopStatus::ALL_CLEAR:
      if (buttonState == LOW) {
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED_AND_HELD;
        s_estoppedAt  = s_lastEStopButtonStateChange;
        ESP_LOGI(TAG, "EStop triggered");
        OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
        OpenShock::CommandHandler::SetKeepAlivePaused(true);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_AND_HELD:
      if (buttonState) {
        // User has released the button, now we can trust them holding to clear it.
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED;
        OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED:
      // If the button is held again for the specified time after being released, clear the EStop
      if (!buttonState && !s_lastEStopButtonState && s_lastEStopButtonStateChange + s_estopHoldToClearTime <= OpenShock::millis()) {
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED_CLEARED;
        ESP_LOGI(TAG, "EStop cleared");
        OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_CLEARED:
      // If the button is released, report as ALL_CLEAR
      if (buttonState) {
        s_estopStatus = EStopManager::EStopStatus::ALL_CLEAR;
        ESP_LOGI(TAG, "EStop cleared, all clear");
        OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
        OpenShock::CommandHandler::SetKeepAlivePaused(false);
      }
      break;

    default:
      break;
  }

  s_lastEStopButtonState = buttonState;
}

// TODO?: Allow active HIGH EStops?
void EStopManager::Init(std::uint16_t updateIntervalMs) {
#ifdef OPENSHOCK_ESTOP_PIN
  s_estopPin = OPENSHOCK_ESTOP_PIN;
  pinMode(s_estopPin, INPUT_PULLUP);
  ESP_LOGI(TAG, "Initializing on pin %u", s_estopPin);

  // Start the repeating task, 10Hz may seem slow, but it's plenty fast for an EStop
  TimerHandle_t timer = xTimerCreate(TAG, pdMS_TO_TICKS(updateIntervalMs), pdTRUE, nullptr, _estopManagerTask);
  if (timer == nullptr) {
    ESP_LOGE(TAG, "Failed to create timer!!! Triggering EStop.");
    s_estopStatus = EStopManager::EStopStatus::ESTOPPED;
  } else {
    xTimerStart(timer, 0);
  }

#else
  (void)updateIntervalMs;

  ESP_LOGI(TAG, "EStopManager disabled, no pin defined");
#endif
}

bool EStopManager::IsEStopped() {
  return (s_estopStatus != EStopManager::EStopStatus::ALL_CLEAR);
}

std::int64_t EStopManager::WhenEStopped() {
  if (IsEStopped()) {
    return s_estoppedAt;
  }

  return 0;
}
