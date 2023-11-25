#include "EStopManager.h"

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
static bool s_lastEStopButtonState               = HIGH;

static std::uint8_t s_estopPin;

// TODO?: Allow active HIGH EStops?
void EStopManager::Init() {
#ifdef OPENSHOCK_ESTOP_PIN
  s_estopPin = OPENSHOCK_ESTOP_PIN;
  pinMode(s_estopPin, INPUT_PULLUP);
  ESP_LOGI(TAG, "Initializing on pin %u", s_estopPin);
#else
  ESP_LOGI(TAG, "EStopManager disabled, no pin defined");
#endif
}

bool EStopManager::IsEStopped() {
#ifdef OPENSHOCK_ESTOP_PIN
  return (s_estopStatus != EStopManager::EStopStatus::ALL_CLEAR);
#else
  return false;
#endif
}

std::int64_t EStopManager::WhenEStopped() {
#ifdef OPENSHOCK_ESTOP_PIN
  if (IsEStopped()) {
    return s_estoppedAt;
  } else {
    return 0;
  }
#else
  return 0;
#endif
}

EStopManager::EStopStatus EStopManager::Update() {
#ifdef OPENSHOCK_ESTOP_PIN
  bool buttonState = digitalRead(s_estopPin);
  if (buttonState != s_lastEStopButtonState) {
    s_lastEStopButtonStateChange = OpenShock::millis();
  }
  switch (s_estopStatus) {
    case EStopManager::EStopStatus::ALL_CLEAR:
      if (buttonState == LOW) {
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED_AND_HELD;
        s_estoppedAt  = s_lastEStopButtonStateChange;
        ESP_LOGI(TAG, "Emergency Stopped!!!");
        OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_AND_HELD:
      if (buttonState == HIGH) {
        // User has released the button, now we can trust them holding to clear it.
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED;
        OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED:
      // If the button is held again for the specified time after being released, clear the EStop
      if (buttonState == LOW && s_lastEStopButtonState == LOW && s_lastEStopButtonStateChange + s_estopHoldToClearTime <= OpenShock::millis()) {
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED_CLEARED;
        ESP_LOGI(TAG, "Clearing EStop on button release!");
        OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_CLEARED:
      // If the button is released, report as ALL_CLEAR
      if (buttonState == HIGH) {
        s_estopStatus = EStopManager::EStopStatus::ALL_CLEAR;
        ESP_LOGI(TAG, "All clear!");
        OpenShock::VisualStateManager::SetEmergencyStop(s_estopStatus);
      }
      break;

    default:
      break;
  }

  s_lastEStopButtonState = buttonState;

  return s_estopStatus;
#else
  return EStopManager::EStopStatus::ALL_CLEAR;
#endif
}
