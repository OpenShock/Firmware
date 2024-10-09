#include <freertos/FreeRTOS.h>

#include "VisualStateManager.h"

const char* const TAG = "VisualStateManager";

#include "events/Events.h"
#include "GatewayClientState.h"
#include "Logging.h"
#include "PinPatternManager.h"
#include "RGBPatternManager.h"

#include <WiFi.h>

#include <memory>

#ifndef OPENSHOCK_LED_GPIO
#define OPENSHOCK_LED_GPIO GPIO_NUM_NC
#endif  // OPENSHOCK_LED_GPIO
#ifndef OPENSHOCK_LED_WS2812B
#define OPENSHOCK_LED_WS2812B GPIO_NUM_NC
#endif  // OPENSHOCK_LED_WS2812B

#define REGISTER_EVENT_HANDLER(base, event, handler)                                             \
  do {                                                                                           \
    esp_err_t err = esp_event_handler_register(base, event, handler, nullptr);                   \
    if (err != ESP_OK) {                                                                         \
      OS_LOGE(TAG, "Failed to register event handler for " #event ": %s", esp_err_to_name(err)); \
      return false;                                                                              \
    }                                                                                            \
  } while (0)

const uint64_t kCriticalErrorFlag                = 1 << 0;
const uint64_t kEmergencyStoppedFlag             = 1 << 1;
const uint64_t kEmergencyStopAwaitingReleaseFlag = 1 << 2;
const uint64_t kWebSocketConnectedFlag           = 1 << 3;
const uint64_t kWiFiConnectedFlag                = 1 << 4;
const uint64_t kWiFiScanningFlag                 = 1 << 5;

// Bitmask of when the system is in a "all clear" state.
const uint64_t kStatusOKMask = kWebSocketConnectedFlag | kWiFiConnectedFlag;

static uint64_t s_stateFlags = 0;
static std::shared_ptr<OpenShock::PinPatternManager> s_builtInLedManager;
static std::shared_ptr<OpenShock::RGBPatternManager> s_RGBLedManager;

using namespace OpenShock;

const PinPatternManager::State kCriticalErrorPattern[] = {
  { true, 100}, // LED ON for 0.1 seconds
  {false, 100}  // LED OFF for 0.1 seconds
};
const RGBPatternManager::RGBState kCriticalErrorRGBPattern[] = {
  {255, 0, 0, 100}, // Red ON for 0.1 seconds
  {  0, 0, 0, 100}  // OFF for 0.1 seconds
};

const PinPatternManager::State kEmergencyStoppedPattern[] = {
  { true, 500},
  {false, 500}
};
const RGBPatternManager::RGBState kEmergencyStoppedRGBPattern[] = {
  {255, 0, 0, 500},
  {  0, 0, 0, 500}
};

const PinPatternManager::State kEmergencyStopAwaitingReleasePattern[] = {
  { true, 150},
  {false, 150}
};
const RGBPatternManager::RGBState kEmergencyStopAwaitingReleaseRGBPattern[] = {
  {0, 255, 0, 150},
  {0,   0, 0, 150}
};

const PinPatternManager::State kWiFiDisconnectedPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};
const RGBPatternManager::RGBState kWiFiDisconnectedRGBPattern[] = {
  {0, 0, 255, 100},
  {0, 0,   0, 100},
  {0, 0, 255, 100},
  {0, 0,   0, 100},
  {0, 0, 255, 100},
  {0, 0,   0, 700}
};

const PinPatternManager::State kWiFiConnectedWithoutWSPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};
const RGBPatternManager::RGBState kWiFiConnectedWithoutWSRGBPattern[] = {
  {255, 165, 0, 100},
  {  0,   0, 0, 100},
  {255, 165, 0, 100},
  {  0,   0, 0, 700}
};

const PinPatternManager::State kPingNoResponsePattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};
const RGBPatternManager::RGBState kPingNoResponseRGBPattern[] = {
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 100},
  {0, 50, 255, 100},
  {0,  0,   0, 700}
};

const PinPatternManager::State kWebSocketCantConnectPattern[] = {
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
const RGBPatternManager::RGBState kWebSocketCantConnectRGBPattern[] = {
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

const PinPatternManager::State kWebSocketConnectedPattern[] = {
  { true,    100},
  {false, 10'000}
};
const RGBPatternManager::RGBState kWebSocketConnectedRGBPattern[] = {
  {0, 255, 0,    100},
  {0,   0, 0, 10'000},
};

const PinPatternManager::State kSolidOnPattern[] = {
  {true, 100'000}
};

const PinPatternManager::State kSolidOffPattern[] = {
  {false, 100'000}
};

template<std::size_t N>
inline void _updateVisualStateGPIO(const PinPatternManager::State (&override)[N])
{
  s_builtInLedManager->SetPattern(override);
}

void _updateVisualStateGPIO()
{
  if (s_stateFlags & kCriticalErrorFlag) {
    s_builtInLedManager->SetPattern(kCriticalErrorPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStopAwaitingReleaseFlag) {
    s_builtInLedManager->SetPattern(kEmergencyStopAwaitingReleasePattern);
    return;
  }

  if (s_stateFlags & kEmergencyStoppedFlag) {
    s_builtInLedManager->SetPattern(kEmergencyStoppedPattern);
    return;
  }

  if (s_stateFlags & kWebSocketConnectedFlag) {
    s_builtInLedManager->SetPattern(kWebSocketConnectedPattern);
    return;
  }

  if (s_stateFlags & kWiFiConnectedFlag) {
    s_builtInLedManager->SetPattern(kWiFiConnectedWithoutWSPattern);
    return;
  }

  if (s_stateFlags & kWiFiScanningFlag) {
    s_builtInLedManager->SetPattern(kPingNoResponsePattern);
    return;
  }

  s_builtInLedManager->SetPattern(kWiFiDisconnectedPattern);
}

void _updateVisualStateRGB()
{
  if (s_stateFlags & kCriticalErrorFlag) {
    s_RGBLedManager->SetPattern(kCriticalErrorRGBPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStopAwaitingReleaseFlag) {
    s_RGBLedManager->SetPattern(kEmergencyStopAwaitingReleaseRGBPattern);
    return;
  }

  if (s_stateFlags & kEmergencyStoppedFlag) {
    s_RGBLedManager->SetPattern(kEmergencyStoppedRGBPattern);
    return;
  }

  if (s_stateFlags & kWebSocketConnectedFlag) {
    s_RGBLedManager->SetPattern(kWebSocketConnectedRGBPattern);
    return;
  }

  if (s_stateFlags & kWiFiConnectedFlag) {
    s_RGBLedManager->SetPattern(kWiFiConnectedWithoutWSRGBPattern);
    return;
  }

  if (s_stateFlags & kWiFiScanningFlag) {
    s_RGBLedManager->SetPattern(kPingNoResponseRGBPattern);
    return;
  }

  s_RGBLedManager->SetPattern(kWiFiDisconnectedRGBPattern);
}

void _updateVisualState()
{
  bool gpioActive = s_builtInLedManager != nullptr;
  bool rgbActive  = s_RGBLedManager != nullptr;

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

  OS_LOGW(TAG, "Trying to update visual state, but no LED is active!");
}

void _handleWiFiConnected(arduino_event_t* event)
{
  (void)event;

  uint64_t oldState = s_stateFlags;

  s_stateFlags |= kWiFiConnectedFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
void _handleWiFiDisconnected(arduino_event_t* event)
{
  (void)event;

  uint64_t oldState = s_stateFlags;

  s_stateFlags &= ~kWiFiConnectedFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
void _handleWiFiScanDone(arduino_event_t* event)
{
  (void)event;

  uint64_t oldState = s_stateFlags;

  s_stateFlags &= ~kWiFiScanningFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void _handleOpenShockGatewayStateChanged(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_id;

  uint64_t oldState = s_stateFlags;

  GatewayClientState state = *reinterpret_cast<GatewayClientState*>(event_data);

  switch (state) {
    case GatewayClientState::Connected:
      s_stateFlags |= kWebSocketConnectedFlag;
      break;
    case GatewayClientState::Disconnected:
      s_stateFlags &= ~kWebSocketConnectedFlag;
      break;
    default:
      break;
  }

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

bool VisualStateManager::Init()
{
  bool ledActive = false;

  if (OPENSHOCK_LED_GPIO != GPIO_NUM_NC) {
    s_builtInLedManager = std::make_shared<PinPatternManager>(static_cast<gpio_num_t>(OPENSHOCK_LED_GPIO));
    if (!s_builtInLedManager->IsValid()) {
      OS_LOGE(TAG, "Failed to initialize built-in LED manager");
      return false;
    }
    ledActive = true;
  }

  if (OPENSHOCK_LED_WS2812B != GPIO_NUM_NC) {
    s_RGBLedManager = std::make_shared<RGBPatternManager>(static_cast<gpio_num_t>(OPENSHOCK_LED_WS2812B));
    if (!s_RGBLedManager->IsValid()) {
      OS_LOGE(TAG, "Failed to initialize RGB LED manager");
      return false;
    }
    ledActive = true;
  }

  if (!ledActive) {
    OS_LOGW(TAG, "No LED type is defined, aborting initialization of VisualStateManager");
    return true;
  }

  WiFi.onEvent(_handleWiFiConnected, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_handleWiFiConnected, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_handleWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(_handleWiFiScanDone, ARDUINO_EVENT_WIFI_SCAN_DONE);

  REGISTER_EVENT_HANDLER(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_GATEWAY_STATE_CHANGED, _handleOpenShockGatewayStateChanged);

  // Run the update on init, otherwise no inital pattern is set.
  _updateVisualState();

  return true;
}

void VisualStateManager::SetCriticalError()
{
  uint64_t oldState = s_stateFlags;

  s_stateFlags |= kCriticalErrorFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void VisualStateManager::SetScanningStarted()
{
  uint64_t oldState = s_stateFlags;

  s_stateFlags |= kWiFiScanningFlag;

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void VisualStateManager::SetEmergencyStopStatus(bool isActive, bool isAwaitingRelease)
{
  uint64_t oldState = s_stateFlags;

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
