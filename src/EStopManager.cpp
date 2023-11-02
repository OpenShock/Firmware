#include <EStopManager.h>
#include "Arduino.h"

#include "VisualStateManager.h"

using namespace OpenShock;

static EStopManager::EStopStatus_t s_estopStatus = EStopManager::EStopStatus_t::ALL_CLEAR;
static char s_estopName[20];
static unsigned long s_estopHoldToClearTime = 5000;
static unsigned long s_lastEStopButtonStateChange = 0;
static unsigned long s_estoppedAt = 0;
static bool s_lastEStopButtonState = HIGH;

static std::uint8_t s_estopPin;

// TODO?: Allow active HIGH EStops?
void EStopManager::Init() {
    #ifdef OPENSHOCK_ESTOP_PIN
    s_estopPin = OPENSHOCK_ESTOP_PIN;
    pinMode(s_estopPin, INPUT_PULLUP);
    snprintf(s_estopName, sizeof(s_estopName), "EStop-%u", s_estopPin);
    ESP_LOGI(s_estopName, "EStop: Initializing on pin %u", s_estopPin);
    #else
    snprintf(s_estopName, sizeof(s_estopName), "EStop-XX");
    ESP_LOGI(s_estopName, "EStop: Not configured");
    #endif
}

bool EStopManager::IsEStopped() {
    #ifdef OPENSHOCK_ESTOP_PIN
    return (s_estopStatus != ALL_CLEAR);
    #else
    return false;
    #endif
}

unsigned long EStopManager::WhenEStopped() {
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

EStopManager::EStopStatus_t EStopManager::Update() {
    #ifdef OPENSHOCK_ESTOP_PIN
    bool buttonState = digitalRead(s_estopPin);
    if (buttonState != s_lastEStopButtonState) {
        s_lastEStopButtonStateChange = millis();
    }

    if (s_estopStatus == ALL_CLEAR) {
        if (buttonState == LOW) {
            s_estopStatus = ESTOPPED_AND_HELD;
            s_estoppedAt = s_lastEStopButtonStateChange;
            ESP_LOGI(s_estopName, "Stopped!!");
            OpenShock::VisualStateManager::SetEmergencyStop(true);
        }
    }
    else if (s_estopStatus == ESTOPPED_AND_HELD) {
        if (buttonState == HIGH) {
            // User has released the button, now we can trust them holding to clear it.
            s_estopStatus = ESTOPPED;
        }
    }
    else if (s_estopStatus == ESTOPPED) {
        // If the button is held again for the specified time after being released, clear the EStop
        if (buttonState == LOW && s_lastEStopButtonState == LOW && s_lastEStopButtonStateChange + s_estopHoldToClearTime <= millis()) {
            s_estopStatus = ESTOPPED_CLEARED;
            ESP_LOGI(s_estopName, "Clearing on release!");
            OpenShock::VisualStateManager::SetEmergencyStop(false);
        }
    } else if (s_estopStatus == ESTOPPED_CLEARED) {
        // If the button is released, report as ALL_CLEAR
        if (buttonState == HIGH) {
            s_estopStatus = ALL_CLEAR;
            ESP_LOGI(s_estopName, "All clear!");
        }
    }

    s_lastEStopButtonState = buttonState;

    return s_estopStatus;
    #else
    return ALL_CLEAR;
    #endif
}
