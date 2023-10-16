#include <LedManager.h>
#include "Arduino.h"

namespace LedManager
{
#define ONBOARD_LED_STATE 2

    typedef enum
    {
        ON,
        OFF,
        BLINKING_SLOW,
        BLINKING_FAST
    } LedStatus_t;

    class Led
    {
    private:
        uint8_t _pin;
        unsigned long _lastBlink = 0;
        bool _ledStatus;
        LedStatus_t _lastStatus;

        void OnW()
        {
            if (_ledStatus)
                return;
            digitalWrite(_pin, HIGH);
            _ledStatus = true;
        }

        void OffW()
        {
            if (!_ledStatus)
                return;
            digitalWrite(_pin, LOW);
            _ledStatus = false;
        }

    public:
        Led(uint8_t pin)
        {
            _pin = pin;
            pinMode(pin, OUTPUT);
        }

        void Loop(unsigned long millis)
        {
            switch (_lastStatus)
            {
            case BLINKING_SLOW:
                if (_lastBlink + 1000 <= millis)
                {
                    if (_ledStatus)
                        OffW();
                    else
                        OnW();
                    _lastBlink = millis;
                }
                break;
            case BLINKING_FAST:
                if (_lastBlink + 500 <= millis)
                {
                    if (_ledStatus)
                        OffW();
                    else
                        OnW();
                    _lastBlink = millis;
                }
                break;
            default:
                break;
            }
        }

        void BlinkingSlow()
        {
            _lastStatus = BLINKING_SLOW;
        }

        void BlinkingFast()
        {
            _lastStatus = BLINKING_FAST;
        }

        void On()
        {
            _lastStatus = ON;
            OnW();
        }

        void Off()
        {
            _lastStatus = ON;
            OffW();
        }
    };

    Led blue(ONBOARD_LED_STATE);

    void Loop(wl_status_t wifiStatus, bool webSocketConnected, unsigned long millis)
    {
        if (wifiStatus != WL_CONNECTED)
        {
            blue.Loop(millis);
            blue.BlinkingSlow();
            return;
        }
        if (!webSocketConnected)
        {
            blue.Loop(millis);
            blue.BlinkingFast();
            return;
        }
        blue.Off();
    }
}