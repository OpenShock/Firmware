#include "VisualStateManager.h"

#ifdef OPENSHOCK_LED_GPIO
#ifdef OPENSHOCK_LED_IMPLEMENTATION
#error "Only one LED type can be defined at a time"
#else
#define OPENSHOCK_LED_IMPLEMENTATION
#include "PinPatternManager.h"
#include <Arduino.h>
#endif  // OPENSHOCK_LED_IMPLEMENTATION
#endif  // OPENSHOCK_LED_GPIO

#ifdef OPENSHOCK_LED_WS2812B
#ifdef OPENSHOCK_LED_IMPLEMENTATION
#error "Only one LED type can be defined at a time"
#else
#define OPENSHOCK_LED_IMPLEMENTATION
#endif  // OPENSHOCK_LED_IMPLEMENTATION
#endif  // OPENSHOCK_LED_WS2812B

#include "Logging.h"

#include <WiFi.h>

#if defined(OPENSHOCK_LED_TYPE) && defined(OPENSHOCK_LED_PIN)
#define OPENSHOCK_LED_DEFINED 1
#else
#define OPENSHOCK_LED_DEFINED 0
#endif

const char* const TAG = "VisualStateManager";

constexpr std::uint64_t kCriticalErrorFlag = 1 << 0;
constexpr std::uint64_t kWiFiConnectedFlag = 1 << 1;
constexpr std::uint64_t kWiFiScanningFlag  = 1 << 2;

static std::uint64_t s_stateFlags = 0;

using namespace OpenShock;

#ifdef OPENSHOCK_LED_GPIO

constexpr PinPatternManager::State kCriticalErrorPattern[] = {
  { true, 100}, // LED ON for 0.1 seconds
  {false, 100}  // LED OFF for 0.1 seconds
};

constexpr PinPatternManager::State kWiFiDisconnectedPattern[] = {
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

void _updateVisualState() {
  if (s_stateFlags & kCriticalErrorFlag) {
    s_builtInLedManager.SetPattern(kCriticalErrorPattern);
    return;
  }

  if (s_stateFlags & kWiFiConnectedFlag) {
    s_builtInLedManager.SetPattern(kWiFiConnectedPattern);
    return;
  }

  if (s_stateFlags & kWiFiScanningFlag) {
    s_builtInLedManager.SetPattern(kPingNoResponsePattern);
    return;
  }

  s_builtInLedManager.SetPattern(kWiFiDisconnectedPattern);
}

#endif  // OPENSHOCK_LED_GPIO

#ifdef OPENSHOCK_LED_WS2812B

void _updateVisualState() {
  ESP_LOGE(TAG, "_updateVisualState: (but WS2812B is not implemented yet)");
}

#endif  // OPENSHOCK_LED_WS2812B

#ifndef OPENSHOCK_LED_IMPLEMENTATION

void _updateVisualState() {
  ESP_LOGE(TAG, "_updateVisualState: (but no LED implementation is selected)");
}

#endif  // OPENSHOCK_LED_NONE

void _handleWiFiConnected(arduino_event_t* event) {
  std::uint64_t oldState = s_stateFlags;

  s_stateFlags |= kWiFiConnectedFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
void _handleWiFiDisconnected(arduino_event_t* event) {
  std::uint64_t oldState = s_stateFlags;

  s_stateFlags &= ~kWiFiConnectedFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
void _handleWiFiScanDone(arduino_event_t* event) {
  std::uint64_t oldState = s_stateFlags;

  s_stateFlags &= ~kWiFiScanningFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void VisualStateManager::Init() {
  WiFi.onEvent(_handleWiFiConnected, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_handleWiFiConnected, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_handleWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(_handleWiFiScanDone, ARDUINO_EVENT_WIFI_SCAN_DONE);
}

void VisualStateManager::SetCriticalError() {
  std::uint64_t oldState = s_stateFlags;

  s_stateFlags |= kCriticalErrorFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void VisualStateManager::SetScanningStarted() {
  std::uint64_t oldState = s_stateFlags;

  s_stateFlags |= kWiFiScanningFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
