#include "VisualStateManager.h"

#ifdef OPENSHOCK_LED_GPIO
#include "PinPatternManager.h"
#include <Arduino.h>
#ifndef OPENSHOCK_LED_IMPLEMENTATION
#define OPENSHOCK_LED_IMPLEMENTATION
#endif  // OPENSHOCK_LED_IMPLEMENTATION
#endif  // OPENSHOCK_LED_GPIO

#ifdef OPENSHOCK_LED_WS2812B
#include "RGBPatternManager.h"
#ifndef OPENSHOCK_LED_IMPLEMENTATION
#define OPENSHOCK_LED_IMPLEMENTATION
#endif  // OPENSHOCK_LED_IMPLEMENTATION
#endif  // OPENSHOCK_LED_WS2812B

#ifndef OPENSHOCK_LED_IMPLEMENTATION
#warning "No LED implementation selected, board will not be able to indicate its status visually"
#endif  // OPENSHOCK_LED_IMPLEMENTATION

#include "Logging.h"

#include <WiFi.h>

#if defined(OPENSHOCK_LED_TYPE) && defined(OPENSHOCK_LED_PIN)
#define OPENSHOCK_LED_DEFINED 1
#else
#define OPENSHOCK_LED_DEFINED 0
#endif

const char* const TAG = "VisualStateManager";

constexpr std::uint64_t kCriticalErrorFlag        = 1 << 0;
constexpr std::uint64_t kEmergencyStoppedFlag     = 1 << 1;
constexpr std::uint64_t kEmergencyStopClearedFlag = 1 << 2;
constexpr std::uint64_t kWebSocketConnectedFlag   = 1 << 3;
constexpr std::uint64_t kWiFiConnectedFlag        = 1 << 4;
constexpr std::uint64_t kWiFiScanningFlag         = 1 << 5;

static std::uint64_t s_stateFlags = 0;

// Bitmask of when the system is in a "all clear" state.

constexpr std::uint64_t kStatusOKMask = kWebSocketConnectedFlag | kWiFiConnectedFlag;

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
  { true, 150},
  {false, 150}
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

constexpr PinPatternManager::State kSolidOnPattern[] = {
  {true, 100'000}
};

constexpr PinPatternManager::State kSolidOffPattern[] = {
  {false, 100'000}
};

PinPatternManager s_builtInLedManager(static_cast<gpio_num_t>(OPENSHOCK_LED_GPIO));

template <std::size_t N>
inline void _updateVisualStateGPIO(const PinPatternManager::State (&override)[N]) {
  s_builtInLedManager.SetPattern(override);
}

void _updateVisualStateGPIO() {
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

  if (s_stateFlags & kWiFiConnectedFlag) {
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
  {0, 255, 0, 150},
  {0,   0, 0, 150}
};

constexpr RGBPatternManager::RGBState kWiFiDisconnectedRGBPattern[] = {
  {0, 0, 255, 100},
  {0, 0,   0, 100},
  {0, 0, 255, 100},
  {0, 0,   0, 100},
  {0, 0, 255, 100},
  {0, 0,   0, 700}
};

constexpr RGBPatternManager::RGBState kWiFiConnectedWithoutWSRGBPattern[] = {
  {255, 165, 0, 100},
  {  0,   0, 0, 100},
  {255, 165, 0, 100},
  {  0,   0, 0, 700}
};

constexpr RGBPatternManager::RGBState kPingNoResponseRGBPattern[] = {
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 700}
};

constexpr RGBPatternManager::RGBState kWebSocketCantConnectRGBPattern[] = {
  {255, 0, 0, 100},
  {  0, 0, 0, 100},
  {255, 0, 0, 100},
  {  0, 0, 0, 100},
  {255, 0, 0, 100},
  {  0, 0, 0, 100},
  {255, 0, 0, 100},
  {  0, 0, 0, 100},
  {255, 0, 0, 100},
  {  0, 0, 0, 700}
};

constexpr RGBPatternManager::RGBState kWebSocketConnectedRGBPattern[] = {
  {0, 255, 0,    100},
  {0,   0, 0, 10'000},
};

RGBPatternManager s_RGBLedManager(OPENSHOCK_LED_WS2812B);

void _updateVisualStateRGB() {
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

  if (s_stateFlags & kWiFiConnectedFlag) {
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

void _updateVisualState() {
#ifdef OPENSHOCK_LED_IMPLEMENTATION
#if defined(OPENSHOCK_LED_GPIO) && defined(OPENSHOCK_LED_WS2812B)
  if (s_stateFlags == kStatusOKMask) {
    _updateVisualStateGPIO(kSolidOnPattern);
  } else {
    _updateVisualStateGPIO(kSolidOffPattern);
  }
  _updateVisualStateRGB();
#elif defined(OPENSHOCK_LED_GPIO)
  _updateVisualStateGPIO();
#elif defined(OPENSHOCK_LED_WS2812B)
  _updateVisualStateRGB();
#else
#error "No LED implementation selected but OPENSHOCK_LED_IMPLEMENTATION is defined"
#endif
#else
  ESP_LOGE(TAG, "_updateVisualState: (but no LED implementation is selected)");
  vTaskDelay(10);
#endif
}

void _handleWiFiConnected(arduino_event_t* event) {
  (void)event;

  std::uint64_t oldState = s_stateFlags;

  s_stateFlags |= kWiFiConnectedFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
void _handleWiFiDisconnected(arduino_event_t* event) {
  (void)event;

  std::uint64_t oldState = s_stateFlags;

  s_stateFlags &= ~kWiFiConnectedFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
void _handleWiFiScanDone(arduino_event_t* event) {
  (void)event;
  
  std::uint64_t oldState = s_stateFlags;

  s_stateFlags &= ~kWiFiScanningFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

bool VisualStateManager::Init() {
  WiFi.onEvent(_handleWiFiConnected, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_handleWiFiConnected, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_handleWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(_handleWiFiScanDone, ARDUINO_EVENT_WIFI_SCAN_DONE);

  // Run the update on init, otherwise no inital pattern is set.
  _updateVisualState();

  return true;
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
