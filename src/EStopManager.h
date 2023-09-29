#include "Arduino.h"
#pragma once
namespace EStopManager
{
    typedef enum
    {
        ALL_CLEAR, // The initial, idle state
        ESTOPPED_AND_HELD, // The EStop has been pressed and has not yet been released
        ESTOPPED, // Idle EStopped state
        ESTOPPED_CLEARED // The EStop has been cleared by the user, but we're waiting for the user to release the button (to avoid incidental estops)
    } EStopStatus_t;

    extern unsigned long estoppedAt;

    void Init(uint8_t estopPin);
    EStopStatus_t EStopLoop();
    bool IsEStopped();
}