#include "VisualStateManager.h"

#ifdef OPENSHOCK_LED_GPIO
#ifdef OPENSHOCK_LED_IMPLEMENTATION
#error "Only one LED type can be defined at a time"
#else
#define OPENSHOCK_LED_IMPLEMENTATION
#include "PinPatternManager.h"
#include <Arduino.h>
#endif // OPENSHOCK_LED_IMPLEMENTATION
#endif // OPENSHOCK_LED_GPIO

#ifdef OPENSHOCK_LED_WS2812B
#ifdef OPENSHOCK_LED_IMPLEMENTATION
#error "Only one LED type can be defined at a time"
#else
#define OPENSHOCK_LED_IMPLEMENTATION
#endif // OPENSHOCK_LED_IMPLEMENTATION
#endif // OPENSHOCK_LED_WS2812B

#include <esp_log.h>

#if defined(OPENSHOCK_LED_TYPE) && defined(OPENSHOCK_LED_PIN)
#define OPENSHOCK_LED_DEFINED 1
#else
#define OPENSHOCK_LED_DEFINED 0
#endif

const char* const TAG = "VisualStateManager";

using namespace OpenShock;

#ifdef OPENSHOCK_LED_GPIO

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

PinPatternManager s_builtInLedManager(OPENSHOCK_LED_GPIO);

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

#endif // OPENSHOCK_LED_GPIO

#ifdef OPENSHOCK_LED_WS2812B

void VisualStateManager::SetCriticalError() {
  ESP_LOGE(TAG, "SetCriticalError: (but WS2812B is not implemented yet)");
}

void VisualStateManager::SetWiFiState(WiFiState state) {
  ESP_LOGE(TAG, "SetWiFiState: %d (but WS2812B is not implemented yet)", state);
}

#endif // OPENSHOCK_LED_WS2812B

#ifndef OPENSHOCK_LED_IMPLEMENTATION

void VisualStateManager::SetCriticalError() {
  ESP_LOGW(TAG, "SetCriticalError: (But no LED implementation is selected)");
}

void VisualStateManager::SetWiFiState(WiFiState state) {
  ESP_LOGW(TAG, "SetWiFiState: %d (But no LED implementation is selected)", state);
}

#endif // OPENSHOCK_LED_NONE
