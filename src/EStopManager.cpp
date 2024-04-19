#include "EStopManager.h"

#include "CommandHandler.h"
#include "Common.h"
#include "config/Config.h"
#include "Logging.h"
#include "Time.h"
#include "VisualStateManager.h"

#include <Arduino.h>

const char* const TAG = "EStopManager";

// 10Hz may seem slow, but it's plenty fast for an EStop
const std::uint32_t ESTOP_UPDATE_INTERVAL_MS = 100;

using namespace OpenShock;

static EStopManager::EStopStatus s_estopStatus   = EStopManager::EStopStatus::UNINITIALIZED;
static std::uint32_t s_estopHoldToClearTime      = 5000;
static std::int64_t s_lastEStopButtonStateChange = 0;
static std::int64_t s_estoppedAt                 = 0;
static int s_lastEStopButtonState                = HIGH;

static std::uint8_t s_estopPin = Constants::GPIO_INVALID;

static TimerHandle_t s_estopManagerTimer = nullptr;

void _updateEstopStatus(EStopManager::EStopStatus status) {
  s_estopStatus = status;
  OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);

  if (status == EStopManager::EStopStatus::ALL_CLEAR) {
    // If we're all clear, we can resume keepalives
    OpenShock::CommandHandler::SetKeepAlivePaused(false);
  } else {
    // Otherwise, temporarily suppress them.
    OpenShock::CommandHandler::SetKeepAlivePaused(true);
  }
}

void _estopManagerTask(TimerHandle_t xTimer) {
  configASSERT(xTimer);

  int buttonState = digitalRead(s_estopPin);
  if (buttonState != s_lastEStopButtonState) {
    s_lastEStopButtonStateChange = OpenShock::millis();
  }
  switch (s_estopStatus) {
    case EStopManager::EStopStatus::UNINITIALIZED:
      // s_estopStatus = EStopManager::EStopStatus::ALL_CLEAR;
      // OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      // OpenShock::CommandHandler::SetKeepAlivePaused(false);
      _updateEstopStatus(EStopManager::EStopStatus::ALL_CLEAR);
      break;
    case EStopManager::EStopStatus::ALL_CLEAR:
      if (buttonState == LOW) {
        // s_estopStatus = EStopManager::EStopStatus::ESTOPPED_AND_HELD;
        s_estoppedAt = s_lastEStopButtonStateChange;
        _updateEstopStatus(EStopManager::EStopStatus::ESTOPPED_AND_HELD);
        ESP_LOGI(TAG, "EStop triggered");
        // OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
        // OpenShock::CommandHandler::SetKeepAlivePaused(true);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_AND_HELD:
      if (buttonState == HIGH) {
        // User has released the button, now we can trust them holding to clear it.
        // s_estopStatus = EStopManager::EStopStatus::ESTOPPED;
        // OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
        _updateEstopStatus(EStopManager::EStopStatus::ESTOPPED);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED:
      // If the button is held again for the specified time after being released, clear the EStop
      if (buttonState == LOW && s_lastEStopButtonState == LOW && s_lastEStopButtonStateChange + s_estopHoldToClearTime <= OpenShock::millis()) {
        // s_estopStatus = EStopManager::EStopStatus::ESTOPPED_CLEARED;
        // OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
        _updateEstopStatus(EStopManager::EStopStatus::ESTOPPED_CLEARED);
        ESP_LOGI(TAG, "EStop cleared");
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_CLEARED:
      // If the button is released, report as ALL_CLEAR
      if (buttonState == HIGH) {
        // s_estopStatus = EStopManager::EStopStatus::ALL_CLEAR;
        // OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
        // OpenShock::CommandHandler::SetKeepAlivePaused(false);
        _updateEstopStatus(EStopManager::EStopStatus::ALL_CLEAR);
        ESP_LOGI(TAG, "EStop cleared, all clear");
      }
      break;

    default:
      break;
  }

  s_lastEStopButtonState = buttonState;
}

// TODO?: Allow active HIGH EStops?
bool EStopManager::Init(std::uint8_t estopPin) {
  // Re-initialize variables in case we're changing the pin
  s_estoppedAt                 = 0;
  s_lastEStopButtonStateChange = 0;
  s_estopPin                   = estopPin;
  s_lastEStopButtonState       = HIGH;

  if (estopPin == Constants::GPIO_INVALID) {
    ESP_LOGE(TAG, "GPIO_INVALID supplied, assuming EStop is disabled.");
    _updateEstopStatus(EStopManager::EStopStatus::ALL_CLEAR);
    return true;
  } else {
    pinMode(s_estopPin, INPUT_PULLUP);
    ESP_LOGI(TAG, "Initializing EStopManager on pin %u", s_estopPin);

    // Start the repeating task
    s_estopManagerTimer = xTimerCreate(TAG, pdMS_TO_TICKS(ESTOP_UPDATE_INTERVAL_MS), pdTRUE, nullptr, _estopManagerTask);
    if (s_estopManagerTimer == nullptr) {
      ESP_LOGE(TAG, "Failed to create timer!!! Triggering EStop.");
      // s_estopStatus = EStopManager::EStopStatus::ESTOPPED;
      // OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      _updateEstopStatus(EStopManager::EStopStatus::ESTOPPED);
      return false;
    } else {
      xTimerStart(s_estopManagerTimer, 0);
      return true;
    }
  }
}

bool EStopManager::Destroy() {
  if (s_estopStatus == EStopManager::EStopStatus::UNINITIALIZED) {
    return true;
  }
  if (xTimerDelete(s_estopManagerTimer, pdMS_TO_TICKS(500)) != pdPASS) {
    ESP_LOGE(TAG, "Failed to delete timer!!!");
    return false;
  } else {
    s_estopManagerTimer = nullptr;
    // s_estopStatus = EStopManager::EStopStatus::UNINITIALIZED;
    // OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
    _updateEstopStatus(EStopManager::EStopStatus::UNINITIALIZED);
    return true;
  }
}

bool EStopManager::IsEStopped() {
  return (s_estopStatus != EStopManager::EStopStatus::ALL_CLEAR);
}

bool EStopManager::IsRunning() {
  return (s_estopStatus != EStopManager::EStopStatus::UNINITIALIZED && s_estopManagerTimer != nullptr);
}

std::int64_t EStopManager::WhenEStopped() {
  if (IsEStopped()) {
    return s_estoppedAt;
  }

  return 0;
}
