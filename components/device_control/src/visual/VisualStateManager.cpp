#include <freertos/FreeRTOS.h>

#include "visual/VisualStateManager.h"

const char* const TAG = "VisualStateManager";

#include "BoardDescriptor.h"
#include "Chipset.h"
#include "enums/GatewayClientState.h"
#include "estop/EStopState.h"
#include "events/Events.h"
#include "led_drivers/MonoLedDriver.h"
#include "led_drivers/RgbLedDriver.h"
#include "Logging.h"
#include "SimpleMutex.h"
#include "visual/LedConfig.h"

#include <esp_netif.h>
#include <esp_wifi.h>

#include <algorithm>
#include <atomic>
#include <memory>

// Status LED pins come from the board descriptor stamped into the image (BoardDescriptor.h);
// -1 (== GPIO_NUM_NC) means the board has no such LED.

const uint32_t kCriticalErrorFlag                = 1 << 0;
const uint32_t kEmergencyStoppedFlag             = 1 << 1;
const uint32_t kEmergencyStopActiveClearingFlag  = 1 << 2;
const uint32_t kEmergencyStopAwaitingReleaseFlag = 1 << 3;
const uint32_t kWebSocketConnectedFlag           = 1 << 4;
const uint32_t kHasIpAddressFlag                 = 1 << 5;
const uint32_t kWiFiConnectedFlag                = 1 << 6;
const uint32_t kWiFiScanningFlag                 = 1 << 7;

// Bitmask of when the system is running normally
const uint32_t kStatusOKMask = kWebSocketConnectedFlag | kHasIpAddressFlag | kWiFiConnectedFlag;

// Critical states: shown in LedMode::CriticalOnly and at the critical brightness. Their patterns win over every other
const uint32_t kCriticalMask = kCriticalErrorFlag | kEmergencyStoppedFlag | kEmergencyStopActiveClearingFlag | kEmergencyStopAwaitingReleaseFlag;

// 32-bit so the atomic is lock-free. Both need seq_cst so a state change during the LED test isn't lost.
static std::atomic<uint32_t> s_stateFlags = 0;
static std::atomic<bool> s_ledTestActive  = false;  // While set, the LED test owns the LEDs; flags keep tracking live state
static OpenShock::SimpleMutex s_applyMutex;         // Serializes reading the flags with applying them, so an older snapshot can't land last
static std::unique_ptr<OpenShock::MonoLedDriver> s_monoLedDriver;
static std::unique_ptr<OpenShock::RgbLedDriver> s_rgbLedDriver;

// The `led` section's settings, loaded in Init. Each is atomic on its own; a setter re-applies after storing, so a mix
// of old and new values read mid-update is replaced at once.
static std::atomic<OpenShock::LedMode> s_ledMode = OpenShock::LedMode::All;
static std::atomic<uint8_t> s_monoCriticalBrightness = 100;  // Percent of full output
static std::atomic<uint8_t> s_monoNormalBrightness   = 100;
static std::atomic<uint8_t> s_rgbCriticalBrightness  = 8;
static std::atomic<uint8_t> s_rgbNormalBrightness    = 8;

// RGB brightness above this gets a warning: a WS2812B is glaring well before full output
static constexpr uint8_t kRgbBrightnessWarnPercent = 30;

static std::atomic<uint8_t>& brightnessSetting(OpenShock::LedType type, OpenShock::LedSignal signal)
{
  bool critical = signal == OpenShock::LedSignal::Critical;
  if (type == OpenShock::LedType::Mono) {
    return critical ? s_monoCriticalBrightness : s_monoNormalBrightness;
  }
  return critical ? s_rgbCriticalBrightness : s_rgbNormalBrightness;
}

// Percent (0-100) to the drivers' 0-255 scale, rounded
static inline uint8_t toDriverBrightness(uint8_t percent)
{
  return static_cast<uint8_t>((static_cast<uint32_t>(percent) * 255 + 50) / 100);
}

// Sets or clears `flag` and returns whether that changed it. Atomic per flag, so concurrent setters (the event loop,
// SetCriticalError, SetScanningStarted) each see their own change and none is missed.
static inline bool setStateFlag(uint32_t flag, bool state)
{
  uint32_t previous = state ? s_stateFlags.fetch_or(flag) : s_stateFlags.fetch_and(~flag);
  return ((previous & flag) != 0) != state;
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

const MonoLedDriver::State kWiFiScanningPattern[] = {
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 100},
  { true, 100},
  {false, 700}
};
const RgbLedDriver::RGBState kWiFiScanningRGBPattern[] = {
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
const RgbLedDriver::RGBState kSolidOffRGBPattern[] = {
  {0, 0, 0, 100'000}
};

template<std::size_t N>
static inline void updateVisualStateGPIO(const MonoLedDriver::State (&override)[N])
{
  s_monoLedDriver->SetPattern(override);
}

// Check-Set-Return pattern for setting a pattern based on a flag
#define CSR_PATTERN(manager, flag, pattern) \
  if ((flags & (flag)) != 0) {              \
    manager->SetPattern(pattern);           \
    return;                                 \
  }

static void updateVisualStateGPIO(uint32_t flags)
{
  CSR_PATTERN(s_monoLedDriver, kCriticalErrorFlag, kCriticalErrorPattern);
  CSR_PATTERN(s_monoLedDriver, kEmergencyStopAwaitingReleaseFlag, kEmergencyStopAwaitingReleasePattern);
  CSR_PATTERN(s_monoLedDriver, kEmergencyStopActiveClearingFlag, kEmergencyStopActiveClearingPattern);
  CSR_PATTERN(s_monoLedDriver, kEmergencyStoppedFlag, kEmergencyStoppedPattern);
  CSR_PATTERN(s_monoLedDriver, kWebSocketConnectedFlag, kWebSocketConnectedPattern);
  CSR_PATTERN(s_monoLedDriver, kHasIpAddressFlag, kWiFiConnectedWithoutWSPattern);
  CSR_PATTERN(s_monoLedDriver, kWiFiScanningFlag, kWiFiScanningPattern);

  s_monoLedDriver->SetPattern(kWiFiDisconnectedPattern);
}

static void updateVisualStateRGB(uint32_t flags)
{
  CSR_PATTERN(s_rgbLedDriver, kCriticalErrorFlag, kCriticalErrorRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kEmergencyStopAwaitingReleaseFlag, kEmergencyStopAwaitingReleaseRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kEmergencyStopActiveClearingFlag, kEmergencyStopActiveClearingRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kEmergencyStoppedFlag, kEmergencyStoppedRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kWebSocketConnectedFlag, kWebSocketConnectedRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kHasIpAddressFlag, kWiFiConnectedWithoutWSRGBPattern);
  CSR_PATTERN(s_rgbLedDriver, kWiFiScanningFlag, kWiFiScanningRGBPattern);

  s_rgbLedDriver->SetPattern(kWiFiDisconnectedRGBPattern);
}

// `showAll` ignores the LED mode, for the LED test; the brightness settings still apply.
static void applyVisualState(uint32_t flags, bool showAll = false)
{
  bool gpioActive = s_monoLedDriver != nullptr;
  bool rgbActive  = s_rgbLedDriver != nullptr;

  bool critical = (flags & kCriticalMask) != 0;
  LedMode mode  = showAll ? LedMode::All : s_ledMode.load();
  if (mode == LedMode::Off || (mode == LedMode::CriticalOnly && !critical)) {
    if (gpioActive) {
      s_monoLedDriver->SetPattern(kSolidOffPattern);
    }
    if (rgbActive) {
      s_rgbLedDriver->SetPattern(kSolidOffRGBPattern);
    }
    return;
  }

  // Before SetPattern: the pattern task applies the brightness with each step, and SetPattern restarts the pattern,
  // so even a long solid step picks up a new brightness at once
  LedSignal signal = critical ? LedSignal::Critical : LedSignal::Normal;
  if (gpioActive) {
    s_monoLedDriver->SetBrightness(toDriverBrightness(brightnessSetting(LedType::Mono, signal).load()));
  }
  if (rgbActive) {
    s_rgbLedDriver->SetBrightness(toDriverBrightness(brightnessSetting(LedType::Rgb, signal).load()));
  }

  if (gpioActive && rgbActive) {
    if (flags == kStatusOKMask) {
      updateVisualStateGPIO(kSolidOnPattern);
    } else {
      updateVisualStateGPIO(kSolidOffPattern);
    }
    updateVisualStateRGB(flags);
    return;
  }

  if (gpioActive) {
    updateVisualStateGPIO(flags);
    return;
  }

  if (rgbActive) {
    updateVisualStateRGB(flags);
    return;
  }

  OS_LOGW(TAG, "Trying to update visual state, but no LED is active!");
}

static void updateVisualState()
{
  OpenShock::ScopedLock lock__(&s_applyMutex);

  if (s_ledTestActive.load()) {
    return;  // Re-applied from the live flags when the test finishes
  }

  applyVisualState(s_stateFlags.load());
}

static void handleEspWiFiEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_data;

  bool changed = false;

  switch (event_id) {
    case WIFI_EVENT_STA_CONNECTED:
      changed = setStateFlag(kWiFiConnectedFlag, true);
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      changed = setStateFlag(kWiFiConnectedFlag, false);
      changed |= setStateFlag(kHasIpAddressFlag, false);
      break;
    case WIFI_EVENT_SCAN_DONE:
      changed = setStateFlag(kWiFiScanningFlag, false);
      break;
    default:
      return;
  }

  if (changed) {
    updateVisualState();
  }
}

static void handleEspIpEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_data;

  bool changed = false;

  switch (event_id) {
    case IP_EVENT_GOT_IP6:
    case IP_EVENT_STA_GOT_IP:
    case IP_EVENT_ETH_GOT_IP:
    case IP_EVENT_PPP_GOT_IP:
      changed = setStateFlag(kHasIpAddressFlag, true);
      break;
    case IP_EVENT_STA_LOST_IP:
    case IP_EVENT_ETH_LOST_IP:
    case IP_EVENT_PPP_LOST_IP:
      changed = setStateFlag(kHasIpAddressFlag, false);
      break;
    default:
      return;
  }

  if (changed) {
    updateVisualState();
  }
}

static bool handleOpenShockEStopStateChanged(void* event_data)
{
  auto state = *static_cast<EStopState*>(event_data);

  bool changed = setStateFlag(kEmergencyStoppedFlag, state != EStopState::Idle);
  changed |= setStateFlag(kEmergencyStopActiveClearingFlag, state == EStopState::ActiveClearing);
  changed |= setStateFlag(kEmergencyStopAwaitingReleaseFlag, state == EStopState::AwaitingRelease);
  return changed;
}

static bool handleOpenShockGatewayStateChanged(void* event_data)
{
  auto state = *static_cast<GatewayClientState*>(event_data);

  return setStateFlag(kWebSocketConnectedFlag, state == GatewayClientState::Connected);
}

static void handleOpenShockEvent(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;

  bool changed = false;

  switch (event_id) {
    case OPENSHOCK_EVENT_ESTOP_STATE_CHANGED:
      changed = handleOpenShockEStopStateChanged(event_data);
      break;
    case OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED:
      changed = handleOpenShockGatewayStateChanged(event_data);
      break;
    case OPENSHOCK_EVENT_WIFI_STATE_CHANGED:
      // WiFi state is tracked from the IDF WIFI_EVENT/IP_EVENT handlers above.
      return;
    default:
      OS_LOGW(TAG, "Received unknown event ID: %d", static_cast<int>(event_id));
      return;
  }

  if (changed) {
    updateVisualState();
  }
}

bool VisualStateManager::Init()
{
  bool ledActive = false;

  const BoardDescriptor& board = Board::Get();

  // The pins come from the stamped descriptor, so they are data, not something the build checked. A pin this
  // chip cannot drive leaves that LED off rather than failing Init: that would restart-loop a normal boot and
  // roll back an OTA boot over an indicator. ValidateBoardDescriptor() has already logged the specifics.
  if (board.ledGpio != GPIO_NUM_NC) {
    if (!OpenShock::IsValidOutputPin(board.ledGpio)) {
      OS_LOGW(TAG, "Built-in LED pin %hhi is not usable on this board, leaving the LED disabled", board.ledGpio);
    } else {
      s_monoLedDriver = std::make_unique<MonoLedDriver>(static_cast<gpio_num_t>(board.ledGpio));
      if (!s_monoLedDriver->IsValid()) {
        OS_LOGE(TAG, "Failed to initialize built-in LED manager");
        return false;
      }
      ledActive = true;
    }
  }

  if (board.ws2812bGpio != GPIO_NUM_NC) {
    if (!OpenShock::IsValidOutputPin(board.ws2812bGpio)) {
      OS_LOGW(TAG, "RGB LED pin %hhi is not usable on this board, leaving the LED disabled", board.ws2812bGpio);
    } else {
      s_rgbLedDriver = std::make_unique<RgbLedDriver>(static_cast<gpio_num_t>(board.ws2812bGpio));
      if (!s_rgbLedDriver->IsValid()) {
        OS_LOGE(TAG, "Failed to initialize RGB LED manager");
        return false;
      }
      ledActive = true;
    }
  }

  if (!ledActive) {
    OS_LOGW(TAG, "No LED type is defined, aborting initialization of VisualStateManager");
    return true;
  }

  const Config::LedConfig ledConfig = Config::GetLedConfig();
  s_ledMode.store(ledConfig.mode);
  for (LedType type : {LedType::Mono, LedType::Rgb}) {
    for (LedSignal signal : {LedSignal::Critical, LedSignal::Normal}) {
      brightnessSetting(type, signal).store(ledConfig.Brightness(type, signal));
    }
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

LedMode VisualStateManager::GetLedMode()
{
  return s_ledMode.load();
}

bool VisualStateManager::SetLedMode(LedMode mode)
{
  if (!Config::SetLedMode(mode)) {
    OS_LOGE(TAG, "Failed to save the LED mode to config");
    return false;
  }

  if (s_ledMode.exchange(mode) != mode) {
    updateVisualState();
  }

  return true;
}

uint8_t VisualStateManager::GetLedBrightness(LedType type, LedSignal signal)
{
  return brightnessSetting(type, signal).load();
}

bool VisualStateManager::SetLedBrightness(LedType type, LedSignal signal, uint8_t percent)
{
  percent = std::min(percent, Config::LedConfig::kMaxBrightness);

  if (!Config::SetLedBrightness(type, signal, percent)) {
    OS_LOGE(TAG, "Failed to save the LED brightness to config");
    return false;
  }

  if (type == LedType::Rgb && percent > kRgbBrightnessWarnPercent) {
    OS_LOGW(TAG, "RGB LED brightness set to %u%%: above %u%% a WS2812B is glaring and runs warm", percent, kRgbBrightnessWarnPercent);
  }

  if (brightnessSetting(type, signal).exchange(percent) != percent) {
    updateVisualState();
  }

  return true;
}

void VisualStateManager::SetCriticalError()
{
  if (setStateFlag(kCriticalErrorFlag, true)) {
    updateVisualState();
  }
}

void VisualStateManager::SetScanningStarted()
{
  if (setStateFlag(kWiFiScanningFlag, true)) {
    updateVisualState();
  }
}

void VisualStateManager::RunLedTest()
{
  // Take over the LEDs; state events keep updating s_stateFlags but don't touch the LEDs until the test ends.
  // Under s_applyMutex so an update already in flight finishes before the test starts.
  {
    ScopedLock lock__(&s_applyMutex);
    if (s_ledTestActive.exchange(true)) {
      OS_LOGW(TAG, "LED test already running");
      return;
    }
  }

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

    s_monoLedDriver->ClearPattern();
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // --- Phase 2: RGB LED color test ---
  if (s_rgbLedDriver != nullptr) {
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

    s_rgbLedDriver->ClearPattern();
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // --- Phase 3: State pattern test (both LEDs together) ---
  struct TestStep {
    const char* name;
    uint32_t flags;
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
    applyVisualState(step.flags, true);
    vTaskDelay(pdMS_TO_TICKS(step.durationMs));
  }

  // Show the live state again (including anything that changed during the test, e.g. an E-Stop)
  OS_LOGI(TAG, "=== LED Test Complete, restoring state ===");
  s_ledTestActive.store(false);
  updateVisualState();
}
