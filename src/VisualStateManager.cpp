#include <freertos/FreeRTOS.h>

#include "VisualStateManager.h"

const char* const TAG = "VisualStateManager";

#include "estop/EStopState.h"
#include "events/Events.h"
#include "GatewayClientState.h"
#include "Logging.h"
#include "PinPatternManager.h"
#include "RGBPatternManager.h"

#include <memory>

#ifndef OPENSHOCK_LED_GPIO
#define OPENSHOCK_LED_GPIO GPIO_NUM_NC
#endif  // OPENSHOCK_LED_GPIO
#ifndef OPENSHOCK_LED_WS2812B
#define OPENSHOCK_LED_WS2812B GPIO_NUM_NC
#endif  // OPENSHOCK_LED_WS2812B

const uint64_t kCriticalErrorFlag                = 1 << 0;
const uint64_t kEmergencyStoppedFlag             = 1 << 1;
const uint64_t kEmergencyStopActiveClearingFlag  = 1 << 2;
const uint64_t kEmergencyStopAwaitingReleaseFlag = 1 << 3;
const uint64_t kWebSocketConnectedFlag           = 1 << 4;
const uint64_t kHasIpAddressFlag                 = 1 << 5;
const uint64_t kWiFiConnectedFlag                = 1 << 6;
const uint64_t kWiFiScanningFlag                 = 1 << 7;

// Bitmask of when the system is running normally
const uint64_t kStatusOKMask = kWebSocketConnectedFlag | kHasIpAddressFlag | kWiFiConnectedFlag;

static uint64_t s_stateFlags = 0;
static std::shared_ptr<OpenShock::PinPatternManager> s_builtInLedManager;
static std::shared_ptr<OpenShock::RGBPatternManager> s_RGBLedManager;

inline void _setStateFlag(uint64_t flag, bool state)
{
  if (state) {
    s_stateFlags |= flag;
  } else {
    s_stateFlags &= ~flag;
  }
}

inline bool _isStateFlagSet(uint64_t flag)
{
  return s_stateFlags & flag;
}

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
const RGBPatternManager::RGBState kEmergencyStopActiveClearingRGBPattern[] = {
  {0,   0, 0, 50},
  {64,   0, 0, 50},
  {128,   0, 0, 50},
  {192,   0, 0, 50},
  {255,   0, 0, 50},
  {192,   0, 0, 50},
  {128,   0, 0, 50},
  {64,   0, 0, 50},
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

// Check-Set-Return pattern for setting a pattern based on a flag
#define CSR_PATTERN(manager, flag, pattern) \
  if (_isStateFlagSet(flag)) {              \
    manager->SetPattern(pattern);           \
    return;                                 \
  }

void _updateVisualStateGPIO()
{
  CSR_PATTERN(s_builtInLedManager, kCriticalErrorFlag, kCriticalErrorPattern);
  CSR_PATTERN(s_builtInLedManager, kEmergencyStopAwaitingReleaseFlag, kEmergencyStopAwaitingReleasePattern);
  CSR_PATTERN(s_builtInLedManager, kEmergencyStopActiveClearingFlag, kEmergencyStopAwaitingReleasePattern);
  CSR_PATTERN(s_builtInLedManager, kEmergencyStoppedFlag, kEmergencyStoppedPattern);
  CSR_PATTERN(s_builtInLedManager, kWebSocketConnectedFlag, kWebSocketConnectedPattern);
  CSR_PATTERN(s_builtInLedManager, kHasIpAddressFlag, kWiFiConnectedWithoutWSPattern);
  CSR_PATTERN(s_builtInLedManager, kWiFiScanningFlag, kPingNoResponsePattern);

  s_builtInLedManager->SetPattern(kWiFiDisconnectedPattern);
}

void _updateVisualStateRGB()
{
  CSR_PATTERN(s_RGBLedManager, kCriticalErrorFlag, kCriticalErrorRGBPattern);
  CSR_PATTERN(s_RGBLedManager, kEmergencyStopAwaitingReleaseFlag, kEmergencyStopAwaitingReleaseRGBPattern);
  CSR_PATTERN(s_RGBLedManager, kEmergencyStopActiveClearingFlag, kEmergencyStopActiveClearingRGBPattern);
  CSR_PATTERN(s_RGBLedManager, kEmergencyStoppedFlag, kEmergencyStoppedRGBPattern);
  CSR_PATTERN(s_RGBLedManager, kWebSocketConnectedFlag, kWebSocketConnectedRGBPattern);
  CSR_PATTERN(s_RGBLedManager, kHasIpAddressFlag, kWiFiConnectedWithoutWSRGBPattern);
  CSR_PATTERN(s_RGBLedManager, kWiFiScanningFlag, kPingNoResponseRGBPattern);

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

void _handleEspWiFiEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_data;

  uint64_t oldState = s_stateFlags;

  switch (event_id) {
    case WIFI_EVENT_STA_CONNECTED:
      _setStateFlag(kWiFiConnectedFlag, true);
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      _setStateFlag(kWiFiConnectedFlag, false);
      _setStateFlag(kHasIpAddressFlag, false);
      break;
    case WIFI_EVENT_SCAN_DONE:
      _setStateFlag(kWiFiScanningFlag, false);
      break;
    default:
      return;
  }

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void _handleEspIpEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_data;

  uint64_t oldState = s_stateFlags;

  switch (event_id) {
    case IP_EVENT_GOT_IP6:
    case IP_EVENT_STA_GOT_IP:
    case IP_EVENT_ETH_GOT_IP:
    case IP_EVENT_PPP_GOT_IP:
      _setStateFlag(kHasIpAddressFlag, true);
      break;
    case IP_EVENT_STA_LOST_IP:
    case IP_EVENT_ETH_LOST_IP:
    case IP_EVENT_PPP_LOST_IP:
      _setStateFlag(kHasIpAddressFlag, false);
      break;
    default:
      return;
  }

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void _handleOpenShockEStopStateChanged(void* event_data)
{
  auto state = *reinterpret_cast<EStopState*>(event_data);

  _setStateFlag(kEmergencyStoppedFlag, state != EStopState::Idle);
  _setStateFlag(kEmergencyStopActiveClearingFlag, state == EStopState::ActiveClearing);
  _setStateFlag(kEmergencyStopAwaitingReleaseFlag, state == EStopState::AwaitingRelease);
}

void _handleOpenShockGatewayStateChanged(void* event_data)
{
  auto state = *reinterpret_cast<GatewayClientState*>(event_data);

  _setStateFlag(kWebSocketConnectedFlag, state == GatewayClientState::Connected);
}

void _handleOpenShockEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;

  uint64_t oldState = s_stateFlags;

  switch (event_id) {
    case OPENSHOCK_EVENT_ESTOP_STATE_CHANGED:
      _handleOpenShockEStopStateChanged(event_data);
      break;
    case OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED:
      _handleOpenShockGatewayStateChanged(event_data);
      break;
    default:
      OS_LOGW(TAG, "Received unknown event ID: %i", event_id);
      return;
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

  esp_err_t err;

  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, _handleEspWiFiEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for IP_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, _handleEspIpEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for IP_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(OPENSHOCK_EVENTS, ESP_EVENT_ANY_ID, _handleOpenShockEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for OPENSHOCK_EVENTS: %s", esp_err_to_name(err));
    return false;
  }

  // Run the update on init, otherwise no inital pattern is set.
  _updateVisualState();

  return true;
}

void VisualStateManager::SetCriticalError()
{
  uint64_t oldState = s_stateFlags;

  _setStateFlag(kCriticalErrorFlag, true);

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}

void VisualStateManager::SetScanningStarted()
{
  uint64_t oldState = s_stateFlags;

  _setStateFlag(kWiFiScanningFlag, true);

  if (oldState != s_stateFlags) {
    _updateVisualState();
  }
}
