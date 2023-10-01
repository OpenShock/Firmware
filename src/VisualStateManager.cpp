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

#if defined(OPENSHOCK_LED_TYPE) && defined(OPENSHOCK_LED_PIN) OPENSHOCK_LED_TYPE == PIN
PinPatternManager s_builtInLedManager(OPENSHOCK_LED_PIN);
#endif

void VisualStateManager::SetCriticalError() {
#if defined(OPENSHOCK_LED_TYPE) && defined(OPENSHOCK_LED_PIN) OPENSHOCK_LED_TYPE == PIN
  static bool _state = false;
  if (_state) {
    return;
  }

  s_builtInLedManager.SetPattern(kCriticalErrorPattern);

  _state = true;
#else
  ESP_LOGD(TAG, "SetCriticalError (but LED was not configured at build time)");
#endif
}

void VisualStateManager::SetWiFiState(WiFiState state) {
#if defined(OPENSHOCK_LED_TYPE) && defined(OPENSHOCK_LED_PIN) && OPENSHOCK_LED_TYPE == PIN
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
#else
  ESP_LOGD(TAG, "SetWiFiStateState: %d (but LED was not configured at build time)", state);
#endif
}
