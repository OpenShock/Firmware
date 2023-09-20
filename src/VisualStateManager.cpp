#include "VisualStateManager.h"

#include "PinPatternManager.h"

#include <Arduino.h>

constexpr ShockLink::PinPatternManager::State kCriticalErrorPattern[] = {
    {true, 100}, // LED ON for 0.1 seconds
    {false, 100} // LED OFF for 0.1 seconds
};

constexpr ShockLink::PinPatternManager::State kWiFiErrorPattern[] = {
    {true, 100}, {false, 100},
    {true, 100}, {false, 700}
};

constexpr ShockLink::PinPatternManager::State kWiFiNoConnectionPattern[] = {
    {true, 100}, {false, 100},
    {true, 100}, {false, 100},
    {true, 100}, {false, 700}
};

constexpr ShockLink::PinPatternManager::State kPingNoResponsePattern[] = {
    {true, 100}, {false, 100},
    {true, 100}, {false, 100},
    {true, 100}, {false, 100},
    {true, 100}, {false, 700}
};

constexpr ShockLink::PinPatternManager::State kWebSocketCantConnectPattern[] = {
    {true, 100}, {false, 100},
    {true, 100}, {false, 100},
    {true, 100}, {false, 100},
    {true, 100}, {false, 100},
    {true, 100}, {false, 700}
};

constexpr ShockLink::PinPatternManager::State kWebSocketConnectedPattern[] = {
    {true, 10000}
};

ShockLink::PinPatternManager s_builtInLedManager(LED_BUILTIN);

void ShockLink::VisualStateManager::SetCriticalError() {
    s_builtInLedManager.SetPattern(kCriticalErrorPattern);
}

void ShockLink::VisualStateManager::SetConnectionState(ConnectionState state) {
    switch (state)
    {
    case ConnectionState::WiFi_Error:
        s_builtInLedManager.SetPattern(kWiFiErrorPattern);
        break;
    case ConnectionState::WiFi_NoConnection:
        s_builtInLedManager.SetPattern(kWiFiNoConnectionPattern);
        break;
    case ConnectionState::Ping_NoResponse:
        s_builtInLedManager.SetPattern(kPingNoResponsePattern);
        break;
    case ConnectionState::WebSocket_CantConnect:
        s_builtInLedManager.SetPattern(kWebSocketCantConnectPattern);
        break;
    case ConnectionState::WebSocket_Connected:
        s_builtInLedManager.SetPattern(kWebSocketConnectedPattern);
        break;
    default:
        break;
    }
}