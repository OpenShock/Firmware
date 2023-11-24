#include "EStopManager.h"

#include "Time.h"
#include "Logging.h"
#include "VisualStateManager.h"

#include <Arduino.h>

const char* const TAG = "EStopManager";

namespace OpenShock::EStopManager {
  void Update(TimerHandle_t xTimer);
}

using namespace OpenShock;

static EStopManager::EStopStatus s_estopStatus    = EStopManager::EStopStatus::ALL_CLEAR;
static std::uint32_t s_estopHoldToClearTime       = 5000;
static std::int64_t s_lastEStopButtonStateChange  = 0;
static std::int64_t s_estoppedAt                  = 0;
static bool s_lastEStopButtonState                = HIGH;

static std::uint8_t s_estopPin;

// TODO?: Allow active HIGH EStops?
void EStopManager::Init() {
#ifdef OPENSHOCK_ESTOP_PIN
  s_estopPin = OPENSHOCK_ESTOP_PIN;
  pinMode(s_estopPin, INPUT_PULLUP);
  ESP_LOGI(TAG, "Initializing on pin %u", s_estopPin);

  // Start the repeating task, 10Hz may seem slow, but it's plenty fast for an EStop
  if (xTimerCreate(TAG, pdMS_TO_TICKS(100), pdTRUE, nullptr, EStopManager::Update) == nullptr) {
    ESP_LOGE(TAG, "Failed to create timer");
  }
#else
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

void EStopManager::Update(TimerHandle_t xTimer) {
  configASSERT(xTimer);

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
        OpenShock::VisualStateManager::SetEmergencyStop(true);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_AND_HELD:
      if (buttonState == HIGH) {
        // User has released the button, now we can trust them holding to clear it.
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED;
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED:
      // If the button is held again for the specified time after being released, clear the EStop
      if (buttonState == LOW && s_lastEStopButtonState == LOW && s_lastEStopButtonStateChange + s_estopHoldToClearTime <= OpenShock::millis()) {
        s_estopStatus = EStopManager::EStopStatus::ESTOPPED_CLEARED;
        ESP_LOGI(TAG, "Clearing EStop on button release!");
        OpenShock::VisualStateManager::SetEmergencyStop(false);
      }
      break;
    case EStopManager::EStopStatus::ESTOPPED_CLEARED:
      // If the button is released, report as ALL_CLEAR
      if (buttonState == HIGH) {
        s_estopStatus = EStopManager::EStopStatus::ALL_CLEAR;
        ESP_LOGI(TAG, "All clear!");
      }
      break;

    default:
      break;
  }

  s_lastEStopButtonState = buttonState;
}
