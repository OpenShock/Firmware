#include "VisualStateManager.h"

#include "PinPatternManager.h"

#include <Arduino.h>

const char* const TAG = "VisualStateManager";

using namespace OpenShock;

constexpr PinPatternManager::State kCriticalErrorPattern[] = {
  { true, 100}, // LED ON for 0.1 seconds
  {false, 100}  // LED OFF for 0.1 seconds
};

constexpr PinPatternManager::State kWiFiDisconnected[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};

constexpr PinPatternManager::State kWiFiConnectedPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};

constexpr PinPatternManager::State kPingNoResponsePattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};

constexpr PinPatternManager::State kWebSocketCantConnectPattern[] = {
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

constexpr PinPatternManager::State kWebSocketConnectedPattern[] = {
  {true, 10'000}
};

PinPatternManager s_builtInLedManager(2);

void VisualStateManager::SetCriticalError() {
  static bool _state = false;
  if (_state) {
    return;
  }

  s_builtInLedManager.SetPattern(kCriticalErrorPattern);

  _state = true;
}

void VisualStateManager::SetWiFiState(WiFiState state) {
  static WiFiState _state = (WiFiState)-1;
  if (_state == state) {
    return;
  }

  ESP_LOGD(TAG, "SetWiFiStateState: %d", state);
  switch (state) {
    case WiFiState::Disconnected:
      s_builtInLedManager.SetPattern(kWiFiDisconnected);
      break;
    case WiFiState::Connected:
      s_builtInLedManager.SetPattern(kWiFiConnectedPattern);
      break;
    default:
      return;
  }

  _state = state;
}
