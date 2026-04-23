#include <freertos/FreeRTOS.h>

#include "visual/VisualStateManager.h"

const char* const TAG = "VisualStateManager";

#include "estop/EStopState.h"
#include "events/Events.h"
#include "GatewayClientState.h"
#include "Logging.h"
#include "led_drivers/MonoLedDriver.h"
#include "led_drivers/RgbLedDriver.h"

#include <esp_wifi.h>
#include <esp_netif.h>

#include <atomic>
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

static std::atomic<uint64_t> s_stateFlags = 0;
static std::unique_ptr<OpenShock::MonoLedDriver> s_monoLedDriver;
static std::unique_ptr<OpenShock::RgbLedDriver> s_rgbLedDriver;

static inline void setStateFlag(uint64_t flag, bool state)
{
  if (state) {
    s_stateFlags.fetch_or(flag, std::memory_order_relaxed);
  } else {
    s_stateFlags.fetch_and(~flag, std::memory_order_relaxed);
  }
}

static inline bool isStateFlagSet(uint64_t flag)
{
  return s_stateFlags.load(std::memory_order_relaxed) & flag;
}

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

const MonoLedDriver::State kEmergencyStopActiveClearingPattern[] = {
  { true, 200},
  {false, 200}
};

const RgbLedDriver::RGBState kEmergencyStopActiveClearingRGBPattern[] = {
  {  0,   0, 0, 50},
  { 64,  69, 0, 50},
  {128, 101, 0, 50},
  {192, 133, 0, 50},
  {255, 165, 0, 50},
  {192, 133, 0, 50},
  {128, 101, 0, 50},
  { 64,  69, 0, 50},
};

const MonoLedDriver::State kEmergencyStopAwaitingReleasePattern[] = {
  { true, 100},
  {false, 100}
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
static inline void updateVisualStateGPIO(const MonoLedDriver::State (&override)[N])
{
  s_monoLedDriver->SetPattern(override);
}

// Check-Set-Return pattern for setting a pattern based on a flag
#define CSR_PATTERN(manager, flag, pattern) \
  if (isStateFlagSet(flag)) {               \
    manager->SetPattern(pattern);           \
    return;                                 \
  }

static void updateVisualStateGPIO()
{
  CSR_PATTERN(s_monoLedDriver, kCriticalErrorFlag, kCriticalErrorPattern);
  CSR_PATTERN(s_monoLedDriver, kEmergencyStopAwaitingReleaseFlag, kEmergencyStopAwaitingReleasePattern);
  CSR_PATTERN(s_monoLedDriver, kEmergencyStopActiveClearingFlag, kEmergencyStopActiveClearingPattern);
  CSR_PATTERN(s_monoLedDriver, kEmergencyStoppedFlag, kEmergencyStoppedPattern);
  CSR_PATTERN(s_monoLedDriver, kWebSocketConnectedFlag, kWebSocketConnectedPattern);
  CSR_PATTERN(s_monoLedDriver, kHasIpAddressFlag, kWiFiConnectedWithoutWSPattern);
  CSR_PATTERN(s_monoLedDriver, kWiFiScanningFlag, kPingNoResponsePattern);

  s_monoLedDriver->SetPattern(kWiFiDisconnectedPattern);
}

static void updateVisualStateRGB()
{
  CSR_PATTERN(s_rgbLedDriver, kCriticalErrorFlag, kCriticalErrorRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kEmergencyStopAwaitingReleaseFlag, kEmergencyStopAwaitingReleaseRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kEmergencyStopActiveClearingFlag, kEmergencyStopActiveClearingRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kEmergencyStoppedFlag, kEmergencyStoppedRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kWebSocketConnectedFlag, kWebSocketConnectedRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kHasIpAddressFlag, kWiFiConnectedWithoutWSRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kWiFiScanningFlag, kPingNoResponseRGBPattern);

  s_rgbLedDriver->SetPattern(kWiFiDisconnectedRGBPattern);
}

static void updateVisualState()
{
  bool gpioActive = s_monoLedDriver != nullptr;
  bool rgbActive  = s_rgbLedDriver != nullptr;

  if (gpioActive && rgbActive) {
    if (s_stateFlags == kStatusOKMask) {
      updateVisualStateGPIO(kSolidOnPattern);
    } else {
      updateVisualStateGPIO(kSolidOffPattern);
    }
    updateVisualStateRGB();
    return;
  }

  if (gpioActive) {
    updateVisualStateGPIO();
    return;
  }

  if (rgbActive) {
    updateVisualStateRGB();
    return;
  }

  OS_LOGW(TAG, "Trying to update visual state, but no LED is active!");
}

static void handleEspWiFiEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_data;

  uint64_t oldState = s_stateFlags;

  switch (event_id) {
    case WIFI_EVENT_STA_CONNECTED:
      setStateFlag(kWiFiConnectedFlag, true);
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      setStateFlag(kWiFiConnectedFlag, false);
      setStateFlag(kHasIpAddressFlag, false);
      break;
    case WIFI_EVENT_SCAN_DONE:
      setStateFlag(kWiFiScanningFlag, false);
      break;
    default:
      return;
  }

  if (oldState != s_stateFlags) {
    updateVisualState();
  }
}

static void handleEspIpEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
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
      setStateFlag(kHasIpAddressFlag, true);
      break;
    case IP_EVENT_STA_LOST_IP:
    case IP_EVENT_ETH_LOST_IP:
    case IP_EVENT_PPP_LOST_IP:
      setStateFlag(kHasIpAddressFlag, false);
      break;
    default:
      return;
  }

  if (oldState != s_stateFlags) {
    updateVisualState();
  }
}

static void handleOpenShockEStopStateChanged(void* event_data)
{
  auto state = *static_cast<EStopState*>(event_data);

  setStateFlag(kEmergencyStoppedFlag, state != EStopState::Idle);
  setStateFlag(kEmergencyStopActiveClearingFlag, state == EStopState::ActiveClearing);
  setStateFlag(kEmergencyStopAwaitingReleaseFlag, state == EStopState::AwaitingRelease);
}

static void handleOpenShockGatewayStateChanged(void* event_data)
{
  auto state = *static_cast<GatewayClientState*>(event_data);

  setStateFlag(kWebSocketConnectedFlag, state == GatewayClientState::Connected);
}

static void handleOpenShockEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;

  uint64_t oldState = s_stateFlags;

  switch (event_id) {
    case OPENSHOCK_EVENT_ESTOP_STATE_CHANGED:
      handleOpenShockEStopStateChanged(event_data);
      break;
    case OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED:
      handleOpenShockGatewayStateChanged(event_data);
      break;
    default:
      OS_LOGW(TAG, "Received unknown event ID: %i", event_id);
      return;
  }

  if (oldState != s_stateFlags) {
    updateVisualState();
  }
}

bool VisualStateManager::Init()
{
  bool ledActive = false;

  if (OPENSHOCK_LED_GPIO != GPIO_NUM_NC) {
    s_monoLedDriver = std::make_unique<MonoLedDriver>(static_cast<gpio_num_t>(OPENSHOCK_LED_GPIO));
    if (!s_monoLedDriver->IsValid()) {
      OS_LOGE(TAG, "Failed to initialize built-in LED manager");
      return false;
    }
    ledActive = true;
  }

  if (OPENSHOCK_LED_WS2812B != GPIO_NUM_NC) {
    s_rgbLedDriver = std::make_unique<RgbLedDriver>(static_cast<gpio_num_t>(OPENSHOCK_LED_WS2812B));
    if (!s_rgbLedDriver->IsValid()) {
      OS_LOGE(TAG, "Failed to initialize RGB LED manager");
      return false;
    }
    s_rgbLedDriver->SetBrightness(20);
    ledActive = true;
  }

  if (!ledActive) {
    OS_LOGW(TAG, "No LED type is defined, aborting initialization of VisualStateManager");
    return true;
  }

  esp_err_t err;

  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handleEspWiFiEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for WIFI_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, handleEspIpEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for IP_EVENT: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_event_handler_register(OPENSHOCK_EVENTS, ESP_EVENT_ANY_ID, handleOpenShockEvent, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for OPENSHOCK_EVENTS: %s", esp_err_to_name(err));
    return false;
  }

  // Run the update on init, otherwise no inital pattern is set.
  updateVisualState();

  return true;
}

void VisualStateManager::SetCriticalError()
{
  uint64_t oldState = s_stateFlags;

  setStateFlag(kCriticalErrorFlag, true);

  if (oldState != s_stateFlags) {
    updateVisualState();
  }
}

void VisualStateManager::SetScanningStarted()
{
  uint64_t oldState = s_stateFlags;

  setStateFlag(kWiFiScanningFlag, true);

  if (oldState != s_stateFlags) {
    updateVisualState();
  }
}

void VisualStateManager::RunLedTest()
{
  // Save current state
  uint64_t savedFlags = s_stateFlags.load(std::memory_order_relaxed);

  OS_LOGI(TAG, "=== LED Test Started ===");

  // --- Phase 1: Mono LED brightness sweep (tests LEDC PWM) ---
  if (s_monoLedDriver != nullptr) {
    OS_LOGI(TAG, "  [Mono] Brightness sweep up");
    s_monoLedDriver->ClearPattern();

    static const MonoLedDriver::State solidOn[] = {
      {true, 100'000}
    };
    for (uint16_t b = 0; b <= 255; b += 5) {
      s_monoLedDriver->SetBrightness(static_cast<uint8_t>(b));
      s_monoLedDriver->SetPattern(solidOn);
      vTaskDelay(pdMS_TO_TICKS(20));
    }

    OS_LOGI(TAG, "  [Mono] Brightness sweep down");
    for (int16_t b = 255; b >= 0; b -= 5) {
      s_monoLedDriver->SetBrightness(static_cast<uint8_t>(b));
      s_monoLedDriver->SetPattern(solidOn);
      vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Restore default brightness
    s_monoLedDriver->SetBrightness(255);
    s_monoLedDriver->ClearPattern();
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // --- Phase 2: RGB LED color test ---
  if (s_rgbLedDriver != nullptr) {
    uint8_t savedBrightness = 20;  // default from Init()

    s_rgbLedDriver->SetBrightness(255);

    OS_LOGI(TAG, "  [RGB] Red");
    static const RgbLedDriver::RGBState red[] = {
      {255, 0, 0, 100'000}
    };
    s_rgbLedDriver->SetPattern(red);
    vTaskDelay(pdMS_TO_TICKS(1500));

    OS_LOGI(TAG, "  [RGB] Green");
    static const RgbLedDriver::RGBState green[] = {
      {0, 255, 0, 100'000}
    };
    s_rgbLedDriver->SetPattern(green);
    vTaskDelay(pdMS_TO_TICKS(1500));

    OS_LOGI(TAG, "  [RGB] Blue");
    static const RgbLedDriver::RGBState blue[] = {
      {0, 0, 255, 100'000}
    };
    s_rgbLedDriver->SetPattern(blue);
    vTaskDelay(pdMS_TO_TICKS(1500));

    OS_LOGI(TAG, "  [RGB] White");
    static const RgbLedDriver::RGBState white[] = {
      {255, 255, 255, 100'000}
    };
    s_rgbLedDriver->SetPattern(white);
    vTaskDelay(pdMS_TO_TICKS(1500));

    OS_LOGI(TAG, "  [RGB] Color cycle");
    static const RgbLedDriver::RGBState cycle[] = {
      {255,   0,   0, 500},
      {255, 165,   0, 500},
      {255, 255,   0, 500},
      {  0, 255,   0, 500},
      {  0, 255, 255, 500},
      {  0,   0, 255, 500},
      {128,   0, 255, 500},
      {255,   0, 255, 500},
    };
    s_rgbLedDriver->SetPattern(cycle);
    vTaskDelay(pdMS_TO_TICKS(4000));

    OS_LOGI(TAG, "  [RGB] Brightness sweep");
    static const RgbLedDriver::RGBState solidWhite[] = {
      {255, 255, 255, 100'000}
    };
    for (uint16_t b = 0; b <= 255; b += 5) {
      s_rgbLedDriver->SetBrightness(static_cast<uint8_t>(b));
      s_rgbLedDriver->SetPattern(solidWhite);
      vTaskDelay(pdMS_TO_TICKS(20));
    }
    for (int16_t b = 255; b >= 0; b -= 5) {
      s_rgbLedDriver->SetBrightness(static_cast<uint8_t>(b));
      s_rgbLedDriver->SetPattern(solidWhite);
      vTaskDelay(pdMS_TO_TICKS(20));
    }

    s_rgbLedDriver->SetBrightness(savedBrightness);
    s_rgbLedDriver->ClearPattern();
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // --- Phase 3: State pattern test (both LEDs together) ---
  struct TestStep {
    const char* name;
    uint64_t flags;
    uint32_t durationMs;
  };

  static const TestStep steps[] = {
    {      "WiFi Disconnected",                                                                0, 2000},
    { "WiFi Connected (no WS)",                                                kHasIpAddressFlag, 2000},
    {    "WebSocket Connected", kWebSocketConnectedFlag | kHasIpAddressFlag | kWiFiConnectedFlag, 2000},
    {          "E-Stop Active",                                            kEmergencyStoppedFlag, 2000},
    { "E-Stop Active Clearing",         kEmergencyStoppedFlag | kEmergencyStopActiveClearingFlag, 2000},
    {"E-Stop Awaiting Release",        kEmergencyStoppedFlag | kEmergencyStopAwaitingReleaseFlag, 2000},
    {         "Critical Error",                                               kCriticalErrorFlag, 2000},
  };

  OS_LOGI(TAG, "  State patterns:");
  for (const auto& step : steps) {
    OS_LOGI(TAG, "    %s", step.name);
    s_stateFlags.store(step.flags, std::memory_order_relaxed);
    updateVisualState();
    vTaskDelay(pdMS_TO_TICKS(step.durationMs));
  }

  // Restore original state
  OS_LOGI(TAG, "=== LED Test Complete, restoring state ===");
  s_stateFlags.store(savedFlags, std::memory_order_relaxed);
  updateVisualState();
}
