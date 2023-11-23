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
#include "RGBPatternManager.h"
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

constexpr std::uint64_t kCriticalErrorFlag          = 1 << 0;
constexpr std::uint64_t kEmergencyStoppedFlag       = 1 << 1;
constexpr std::uint64_t kEmergencyStopClearedFlag   = 1 << 2;
constexpr std::uint64_t kWebSocketConnectedFlag     = 1 << 3;
constexpr std::uint64_t kWiFiConnectedWithoutWSFlag = 1 << 4;
constexpr std::uint64_t kWiFiScanningFlag           = 1 << 5;

static std::uint64_t s_stateFlags = 0;

using namespace OpenShock;

#ifdef OPENSHOCK_LED_GPIO

constexpr PinPatternManager::State kCriticalErrorPattern[] = {
  { true, 100}, // LED ON for 0.1 seconds
  {false, 100}  // LED OFF for 0.1 seconds
};

constexpr PinPatternManager::State kEmergencyStoppedPattern[] = {
  { true, 500},
  {false, 500}
};

constexpr PinPatternManager::State kEmergencyStopClearedPattern[] = {
  { true, 200},
  {false, 200}
};

constexpr PinPatternManager::State kWiFiDisconnectedPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};

constexpr PinPatternManager::State kWiFiConnectedWithoutWSPattern[] = {
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
  { true,    100},
  {false, 10'000}
};

PinPatternManager s_builtInLedManager(OPENSHOCK_LED_GPIO);

void _updateVisualState() {
  if (s_stateFlags & kCriticalErrorFlag) {
    s_builtInLedManager.SetPattern(kCriticalErrorPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStoppedFlag) {
    s_builtInLedManager.SetPattern(kEmergencyStoppedPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStopClearedFlag) {
    s_builtInLedManager.SetPattern(kEmergencyStopClearedPattern);
    return;
  }

  if (s_stateFlags & kWebSocketConnectedFlag) {
    s_builtInLedManager.SetPattern(kWebSocketConnectedPattern);
    return;
  }

  if (s_stateFlags & kWiFiConnectedWithoutWSFlag) {
    s_builtInLedManager.SetPattern(kWiFiConnectedWithoutWSPattern);
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

constexpr RGBPatternManager::RGBState kCriticalErrorRGBPattern[] = {
  {255, 0, 0, 100}, // Red ON for 0.1 seconds
  {  0, 0, 0, 100}  // OFF for 0.1 seconds
};

constexpr RGBPatternManager::RGBState kEmergencyStoppedRGBPattern[] = {
  {255, 0, 0, 500},
  {  0, 0, 0, 500}
};

constexpr RGBPatternManager::RGBState kEmergencyStopClearedRGBPattern[] = {
  {0, 255, 0, 200},
  {0,   0, 0, 200}
};

constexpr RGBPatternManager::RGBState kWiFiDisconnectedRGBPattern[] = {
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 700}
};

constexpr RGBPatternManager::RGBState kWiFiConnectedWithoutWSRGBPattern[] = {
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 700}
};

constexpr RGBPatternManager::RGBState kPingNoResponseRGBPattern[] = {
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 700}
};

constexpr RGBPatternManager::RGBState kWebSocketCantConnectRGBPattern[] = {
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 100},
  {0, 0, 100, 100},
  {0, 0,   0, 700}
};

constexpr RGBPatternManager::RGBState kWebSocketConnectedRGBPattern[] = {
  {0, 0, 100,    100},
  {0, 0,   0, 10'000},
};

RGBPatternManager s_RGBLedManager(OPENSHOCK_LED_WS2812B);

void _updateVisualState() {
  if (s_stateFlags & kCriticalErrorFlag) {
    s_RGBLedManager.SetPattern(kCriticalErrorRGBPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStoppedFlag) {
    s_RGBLedManager.SetPattern(kEmergencyStoppedRGBPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStopClearedFlag) {
    s_RGBLedManager.SetPattern(kEmergencyStopClearedRGBPattern);
    return;
  }

  if (s_stateFlags & kWebSocketConnectedFlag) {
    s_RGBLedManager.SetPattern(kWebSocketConnectedRGBPattern);
    return;
  }

  if (s_stateFlags & kWiFiConnectedWithoutWSFlag) {
    s_RGBLedManager.SetPattern(kWiFiConnectedWithoutWSRGBPattern);
    return;
  }

  if (s_stateFlags & kWiFiScanningFlag) {
    s_RGBLedManager.SetPattern(kPingNoResponseRGBPattern);
    return;
  }

  s_RGBLedManager.SetPattern(kWiFiDisconnectedRGBPattern);
}

#endif  // OPENSHOCK_LED_WS2812B

#ifndef OPENSHOCK_LED_IMPLEMENTATION

void _updateVisualState() {
  ESP_LOGE(TAG, "_updateVisualState: (but no LED implementation is selected)");
}

#endif  // OPENSHOCK_LED_NONE

void _handleWiFiConnected(arduino_event_t* event) {
  std::uint64_t oldState = s_stateFlags;

  s_stateFlags |= kWiFiConnectedWithoutWSFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
void _handleWiFiDisconnected(arduino_event_t* event) {
  std::uint64_t oldState = s_stateFlags;

  s_stateFlags &= ~kWiFiConnectedWithoutWSFlag;

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

  // Run the update on init, otherwise no inital pattern is set.
  _updateVisualState();
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

void VisualStateManager::SetEmergencyStop(OpenShock::EStopManager::EStopStatus status) {
  std::uint64_t oldState = s_stateFlags;

  switch (status) {
    // When there is no EStop currently active.
    case OpenShock::EStopManager::EStopStatus::ALL_CLEAR:
      s_stateFlags &= ~kEmergencyStoppedFlag;
      s_stateFlags &= ~kEmergencyStopClearedFlag;
      break;
    // EStop has been triggered!
    case OpenShock::EStopManager::EStopStatus::ESTOPPED_AND_HELD:
    // EStop still active, and user has released the button from the initial trigger.
    case OpenShock::EStopManager::EStopStatus::ESTOPPED:
      s_stateFlags |= kEmergencyStoppedFlag;
      s_stateFlags &= ~kEmergencyStopClearedFlag;
      break;
    // User has held and cleared the EStop, now we're waiting for them to release the button.
    case OpenShock::EStopManager::EStopStatus::ESTOPPED_CLEARED:
      s_stateFlags &= ~kEmergencyStoppedFlag;
      s_stateFlags |= kEmergencyStopClearedFlag;
      break;
    default:
      ESP_LOGE(TAG, "Unhandled EStop status: %d", status);
      break;
  }

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void VisualStateManager::SetWebSocketConnected(bool isConnected) {
  std::uint64_t oldState = s_stateFlags;

  if (isConnected) {
    s_stateFlags |= kWebSocketConnectedFlag;
  } else {
    s_stateFlags &= ~kWebSocketConnectedFlag;
  }

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
