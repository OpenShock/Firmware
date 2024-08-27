#include "visual/VisualStateManager.h"

#include "Logging.h"
#include "visual/MonoLedDriver.h"
#include "visual/RgbLedDriver.h"

#include <WiFi.h>

#include <memory>

const char* const TAG = "VisualStateManager";

const std::uint64_t kCriticalErrorFlag                = 1 << 0;
const std::uint64_t kEmergencyStoppedFlag             = 1 << 1;
const std::uint64_t kEmergencyStopAwaitingReleaseFlag = 1 << 2;
const std::uint64_t kWebSocketConnectedFlag           = 1 << 3;
const std::uint64_t kWiFiConnectedFlag                = 1 << 4;
const std::uint64_t kWiFiScanningFlag                 = 1 << 5;

// Bitmask of when the system is in a "all clear" state.
const std::uint64_t kStatusOKMask = kWebSocketConnectedFlag | kWiFiConnectedFlag;

static std::uint64_t s_stateFlags = 0;
static std::shared_ptr<OpenShock::MonoLedDriver> s_monoLedDriver;
static std::shared_ptr<OpenShock::RgbLedDriver> s_rgbLedDriver;

using namespace OpenShock;

const MonoLedDriver::State kCriticalErrorPattern[] = {
  { true, 100}, // LED ON for 0.1 seconds
  {false, 100}  // LED OFF for 0.1 seconds
};
const RgbLedDriver::RGBState kCriticalErrorRGBPattern[] = {
  {255, 0, 0, 100}, // Red ON for 0.1 seconds
  {  0, 0, 0, 100}  // OFF for 0.1 seconds
};

const MonoLedDriver::State kEmergencyStoppedPattern[] = {
  { true, 500},
  {false, 500}
};
const RgbLedDriver::RGBState kEmergencyStoppedRGBPattern[] = {
  {255, 0, 0, 500},
  {  0, 0, 0, 500}
};

const MonoLedDriver::State kEmergencyStopAwaitingReleasePattern[] = {
  { true, 150},
  {false, 150}
};
const RgbLedDriver::RGBState kEmergencyStopAwaitingReleaseRGBPattern[] = {
  {0, 255, 0, 150},
  {0,   0, 0, 150}
};

const MonoLedDriver::State kWiFiDisconnectedPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};
const RgbLedDriver::RGBState kWiFiDisconnectedRGBPattern[] = {
  {0, 0, 255, 100},
  {0, 0,   0, 100},
  {0, 0, 255, 100},
  {0, 0,   0, 100},
  {0, 0, 255, 100},
  {0, 0,   0, 700}
};

const MonoLedDriver::State kWiFiConnectedWithoutWSPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};
const RgbLedDriver::RGBState kWiFiConnectedWithoutWSRGBPattern[] = {
  {255, 165, 0, 100},
  {  0,   0, 0, 100},
  {255, 165, 0, 100},
  {  0,   0, 0, 700}
};

const MonoLedDriver::State kPingNoResponsePattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};
const RgbLedDriver::RGBState kPingNoResponseRGBPattern[] = {
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 700}
};

const MonoLedDriver::State kWebSocketCantConnectPattern[] = {
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
const RgbLedDriver::RGBState kWebSocketCantConnectRGBPattern[] = {
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

const MonoLedDriver::State kWebSocketConnectedPattern[] = {
  { true,    100},
  {false, 10'000}
};
const RgbLedDriver::RGBState kWebSocketConnectedRGBPattern[] = {
  {0, 255, 0,    100},
  {0,   0, 0, 10'000},
};

const MonoLedDriver::State kSolidOnPattern[] = {
  {true, 100'000}
};

const MonoLedDriver::State kSolidOffPattern[] = {
  {false, 100'000}
};

template<std::size_t N>
inline void _updateVisualStateGPIO(const MonoLedDriver::State (&override)[N]) {
  s_monoLedDriver->SetPattern(override);
}

void _updateVisualStateGPIO() {
  if (s_stateFlags & kCriticalErrorFlag) {
    s_monoLedDriver->SetPattern(kCriticalErrorPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStopAwaitingReleaseFlag) {
    s_monoLedDriver->SetPattern(kEmergencyStopAwaitingReleasePattern);
    return;
  }

  if (s_stateFlags & kEmergencyStoppedFlag) {
    s_monoLedDriver->SetPattern(kEmergencyStoppedPattern);
    return;
  }

  if (s_stateFlags & kWebSocketConnectedFlag) {
    s_monoLedDriver->SetPattern(kWebSocketConnectedPattern);
    return;
  }

  if (s_stateFlags & kWiFiConnectedFlag) {
    s_monoLedDriver->SetPattern(kWiFiConnectedWithoutWSPattern);
    return;
  }

  if (s_stateFlags & kWiFiScanningFlag) {
    s_monoLedDriver->SetPattern(kPingNoResponsePattern);
    return;
  }

  s_monoLedDriver->SetPattern(kWiFiDisconnectedPattern);
}

void _updateVisualStateRGB() {
  if (s_stateFlags & kCriticalErrorFlag) {
    s_rgbLedDriver->SetPattern(kCriticalErrorRGBPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStopAwaitingReleaseFlag) {
    s_rgbLedDriver->SetPattern(kEmergencyStopAwaitingReleaseRGBPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStoppedFlag) {
    s_rgbLedDriver->SetPattern(kEmergencyStoppedRGBPattern);
    return;
  }

  if (s_stateFlags & kWebSocketConnectedFlag) {
    s_rgbLedDriver->SetPattern(kWebSocketConnectedRGBPattern);
    return;
  }

  if (s_stateFlags & kWiFiConnectedFlag) {
    s_rgbLedDriver->SetPattern(kWiFiConnectedWithoutWSRGBPattern);
    return;
  }

  if (s_stateFlags & kWiFiScanningFlag) {
    s_rgbLedDriver->SetPattern(kPingNoResponseRGBPattern);
    return;
  }

  s_rgbLedDriver->SetPattern(kWiFiDisconnectedRGBPattern);
}

void _updateVisualState() {
  bool gpioActive = s_monoLedDriver != nullptr;
  bool rgbActive  = s_rgbLedDriver != nullptr;

  if (gpioActive && rgbActive) {
    if (s_stateFlags == kStatusOKMask) {
      _updateVisualStateGPIO(kSolidOnPattern);
    } else {
      _updateVisualStateGPIO(kSolidOffPattern);
    }
    _updateVisualStateRGB();
    return;
  }

  if (gpioActive) {
    _updateVisualStateGPIO();
    return;
  }

  if (rgbActive) {
    _updateVisualStateRGB();
    return;
  }

  ESP_LOGW(TAG, "Trying to update visual state, but no LED is active!");
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

#ifndef OPENSHOCK_LED_GPIO
#define OPENSHOCK_LED_GPIO GPIO_NUM_NC
#endif  // OPENSHOCK_LED_GPIO
#ifndef OPENSHOCK_LED_WS2812B
#define OPENSHOCK_LED_WS2812B GPIO_NUM_NC
#endif  // OPENSHOCK_LED_WS2812B

bool VisualStateManager::Init() {
  bool ledActive = false;

  if (OPENSHOCK_LED_GPIO != GPIO_NUM_NC) {
    s_monoLedDriver = std::make_shared<MonoLedDriver>(static_cast<gpio_num_t>(OPENSHOCK_LED_GPIO));
    if (!s_monoLedDriver->IsValid()) {
      ESP_LOGE(TAG, "Failed to initialize built-in LED manager");
      return false;
    }
    ledActive = true;
  }

  if (OPENSHOCK_LED_WS2812B != GPIO_NUM_NC) {
    s_rgbLedDriver = std::make_shared<RgbLedDriver>(static_cast<gpio_num_t>(OPENSHOCK_LED_WS2812B));
    if (!s_rgbLedDriver->IsValid()) {
      ESP_LOGE(TAG, "Failed to initialize RGB LED manager");
      return false;
    }
    s_rgbLedDriver->SetBrightness(20);
    ledActive = true;
  }

  if (!ledActive) {
    ESP_LOGW(TAG, "No LED type is defined, aborting initialization of VisualStateManager");
    return true;
  }

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

void VisualStateManager::SetEmergencyStopStatus(bool isActive, bool isAwaitingRelease) {
  std::uint64_t oldState = s_stateFlags;

  if (isActive) {
    s_stateFlags |= kEmergencyStoppedFlag;
  } else {
    s_stateFlags &= ~kEmergencyStoppedFlag;
  }

  if (isAwaitingRelease) {
    s_stateFlags |= kEmergencyStopAwaitingReleaseFlag;
  } else {
    s_stateFlags &= ~kEmergencyStopAwaitingReleaseFlag;
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
