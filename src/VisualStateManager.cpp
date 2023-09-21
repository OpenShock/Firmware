#include "VisualStateManager.h"

#include "PinPatternManager.h"

#include <Arduino.h>

const char* const TAG = "VisualStateManager";

constexpr ShockLink::PinPatternManager::State kCriticalErrorPattern[] = {
  { true, 100}, // LED ON for 0.1 seconds
  {false, 100}  // LED OFF for 0.1 seconds
};

constexpr ShockLink::PinPatternManager::State kWiFiDisconnected[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};

constexpr ShockLink::PinPatternManager::State kWiFiConnectedPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};

constexpr ShockLink::PinPatternManager::State kPingNoResponsePattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};

constexpr ShockLink::PinPatternManager::State kWebSocketCantConnectPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};

constexpr ShockLink::PinPatternManager::State kWebSocketConnectedPattern[] = {
  {true, 10'000}
};

ShockLink::PinPatternManager s_builtInLedManager(2);

static bool s_criticalError = false;
void ShockLink::VisualStateManager::SetCriticalError() {
  if (s_criticalError) {
    return;
  }

  s_builtInLedManager.SetPattern(kCriticalErrorPattern);

  s_criticalError = true;
}

static ShockLink::ConnectionState s_connectionState = (ShockLink::ConnectionState)-1;
void ShockLink::VisualStateManager::SetConnectionState(ConnectionState state) {
  if (s_connectionState == state) {
    return;
  }

  ESP_LOGD(TAG, "SetConnectionState: %d", state);
  switch (state) {
    case ConnectionState::WiFi_Disconnected:
      s_builtInLedManager.SetPattern(kWiFiDisconnected);
      break;
    case ConnectionState::WiFi_Connected:
      s_builtInLedManager.SetPattern(kWiFiConnectedPattern);
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
      return;
  }

  s_connectionState = state;
}
