#include "VisualStateManager.h"

#include "PinPatternManager.h"

#include <Arduino.h>

#if defined(OPENSHOCK_LED_TYPE) && defined(OPENSHOCK_LED_PIN)
#define OPENSHOCK_LED_DEFINED 1
#else
#define OPENSHOCK_LED_DEFINED 0
#endif

const char* const TAG = "VisualStateManager";

#ifdef OPENSHOCK_LED_TYPE

#if OPENSHOCK_LED_TYPE == 0

#define OPENSHOCK_LED_TYPE_NONE

#elif OPENSHOCK_LED_TYPE == 1

#ifdef OPENSHOCK_LED_PIN
#define OPENSHOCK_LED_TYPE_GPIO 1
#else
#error "OPENSHOCK_LED_PIN must be defined when OPENSHOCK_LED_TYPE == 1 (GPIO connected LED)"
#endif

#elif OPENSHOCK_LED_TYPE == 2

#define OPENSHOCK_LED_TYPE_NEOPIXEL

#else
#error "Unsupported LED type"
#endif

#else
#error "OPENSHOCK_LED_TYPE must be defined"
#endif

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

#ifdef OPENSHOCK_LED_TYPE_GPIO
PinPatternManager s_builtInLedManager(OPENSHOCK_LED_PIN);
#endif

void VisualStateManager::SetCriticalError() {
#ifdef OPENSHOCK_LED_TYPE_GPIO
  static bool _state = false;
  if (_state) {
    return;
  }

  s_builtInLedManager.SetPattern(kCriticalErrorPattern);

  _state = true;
#else
  ESP_LOGW(TAG, "SetCriticalError (but LED was not configured at build time)");
#endif
}

void VisualStateManager::SetWiFiState(WiFiState state) {
#ifdef OPENSHOCK_LED_TYPE_GPIO
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
  ESP_LOGW(TAG, "SetWiFiStateState: %d (but LED was not configured at build time)", state);
#endif
}
