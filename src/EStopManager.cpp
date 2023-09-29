#include <EStopManager.h>
#include "Arduino.h"

namespace EStopManager
{

    EStopStatus_t _estopStatus = ALL_CLEAR;
    unsigned long estoppedAt = 0;
    unsigned long _estopHoldToClearTime = 5000;
    unsigned long _lastButtonStateChange = 0;
    bool _lastButtonState = HIGH;
    uint8_t _estopPin = 13;
    // TODO?: Allow active HIGH EStops?

    void Init(uint8_t estopPin) {
        _estopPin = estopPin;
        pinMode(_estopPin, INPUT_PULLUP);
    }

    bool IsEStopped() {
        return (_estopStatus != ALL_CLEAR);
    }

    EStopStatus_t EStopLoop() {
        bool buttonState = digitalRead(_estopPin);
        if (buttonState != _lastButtonState) {
            _lastButtonStateChange = millis();
        }

        if (_estopStatus == ALL_CLEAR) {
            if (buttonState == LOW) {
                _estopStatus = ESTOPPED_AND_HELD;
                estoppedAt = _lastButtonStateChange;
                Serial.println("\nEStop: Stopped!!");
            }
        }
        else if (_estopStatus == ESTOPPED_AND_HELD) {
            if (buttonState == HIGH) {
                // User has released the button, now we can trust them holding to clear it.
                _estopStatus = ESTOPPED;
            }
        }
        else if (_estopStatus == ESTOPPED) {
            // If the button is held again for the specified time after being released, clear the EStop
            if (buttonState == LOW && _lastButtonState == LOW && _lastButtonStateChange + _estopHoldToClearTime <= millis()) {
                _estopStatus = ESTOPPED_CLEARED;
                Serial.println("\nEStop: Clearing on release!");
            }
        } else if (_estopStatus == ESTOPPED_CLEARED) {
            // If the button is released, report as ALL_CLEAR
            if (buttonState == HIGH) {
                _estopStatus = ALL_CLEAR;
                Serial.println("\nEStop: All clear!");
            }
        }

        _lastButtonState = buttonState;

        return _estopStatus;
    }
}