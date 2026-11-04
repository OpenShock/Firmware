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
#include "visual/LedOverrideState.h"

#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <optional>

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
static std::atomic<OpenShock::LedMode> s_ledMode      = OpenShock::LedMode::All;
static std::atomic<uint16_t> s_monoCriticalBrightness = CONFIG_OPENSHOCK_LED_MONO_CRITICAL_BRIGHTNESS;  // 0-65535 of full output
static std::atomic<uint16_t> s_monoNormalBrightness   = CONFIG_OPENSHOCK_LED_MONO_NORMAL_BRIGHTNESS;
static std::atomic<uint16_t> s_rgbCriticalBrightness  = CONFIG_OPENSHOCK_LED_RGB_CRITICAL_BRIGHTNESS;
static std::atomic<uint16_t> s_rgbNormalBrightness    = CONFIG_OPENSHOCK_LED_RGB_NORMAL_BRIGHTNESS;

static std::atomic<uint16_t>& brightnessSetting(OpenShock::LedType type, OpenShock::LedSignal signal)
{
  bool critical = signal == OpenShock::LedSignal::Critical;
  if (type == OpenShock::LedType::Mono) {
    return critical ? s_monoCriticalBrightness : s_monoNormalBrightness;
  }
  return critical ? s_rgbCriticalBrightness : s_rgbNormalBrightness;
}

// The 16-bit brightness (0-65535) to the mono driver's 0-255 scale, rounded. The RGB driver takes it as is and
// reduces it to its 10-bit output.
static inline uint8_t toDriverBrightness(uint16_t brightness)
{
  return static_cast<uint8_t>((static_cast<uint32_t>(brightness) * 255 + 32767) / 65535);
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

template<typename State, std::size_t N>
static constexpr uint32_t cycleMs(const State (&pattern)[N])
{
  uint32_t total = 0;
  for (const State& step : pattern) {
    total += step.duration;
  }
  return total;
}

// The pattern the state flags show. Critical states first: their patterns win over every other.
static VisualStateManager::LedPattern patternForFlags(uint32_t flags)
{
  using VisualStateManager::LedPattern;
  if ((flags & kCriticalErrorFlag) != 0) return LedPattern::CriticalError;
  if ((flags & kEmergencyStopAwaitingReleaseFlag) != 0) return LedPattern::EStopAwaitingRelease;
  if ((flags & kEmergencyStopActiveClearingFlag) != 0) return LedPattern::EStopClearing;
  if ((flags & kEmergencyStoppedFlag) != 0) return LedPattern::EStopped;
  if ((flags & kWebSocketConnectedFlag) != 0) return LedPattern::Connected;
  if ((flags & kHasIpAddressFlag) != 0) return LedPattern::NoBackend;
  if ((flags & kWiFiScanningFlag) != 0) return LedPattern::WiFiScanning;
  return LedPattern::WiFiDisconnected;
}

static bool isCriticalPattern(VisualStateManager::LedPattern pattern)
{
  using VisualStateManager::LedPattern;
  switch (pattern) {
    case LedPattern::CriticalError:
    case LedPattern::EStopped:
    case LedPattern::EStopClearing:
    case LedPattern::EStopAwaitingRelease:
      return true;
    case LedPattern::WiFiDisconnected:
    case LedPattern::WiFiScanning:
    case LedPattern::NoBackend:
    case LedPattern::Connected:
      return false;
  }
  return false;
}

// Calls `fn` with the mono and RGB tables of `pattern`
template<typename F>
static void withPatternTables(VisualStateManager::LedPattern pattern, F&& fn)
{
  using VisualStateManager::LedPattern;
  switch (pattern) {
    case LedPattern::CriticalError:
      fn(kCriticalErrorPattern, kCriticalErrorRGBPattern);
      return;
    case LedPattern::EStopAwaitingRelease:
      fn(kEmergencyStopAwaitingReleasePattern, kEmergencyStopAwaitingReleaseRGBPattern);
      return;
    case LedPattern::EStopClearing:
      fn(kEmergencyStopActiveClearingPattern, kEmergencyStopActiveClearingRGBPattern);
      return;
    case LedPattern::EStopped:
      fn(kEmergencyStoppedPattern, kEmergencyStoppedRGBPattern);
      return;
    case LedPattern::Connected:
      fn(kWebSocketConnectedPattern, kWebSocketConnectedRGBPattern);
      return;
    case LedPattern::NoBackend:
      fn(kWiFiConnectedWithoutWSPattern, kWiFiConnectedWithoutWSRGBPattern);
      return;
    case LedPattern::WiFiScanning:
      fn(kWiFiScanningPattern, kWiFiScanningRGBPattern);
      return;
    case LedPattern::WiFiDisconnected:
      fn(kWiFiDisconnectedPattern, kWiFiDisconnectedRGBPattern);
      return;
  }
  fn(kWiFiDisconnectedPattern, kWiFiDisconnectedRGBPattern);  // Unknown: the pattern with nothing going on
}

// How long a preview of `pattern` lasts: about two cycles of what the board shows it on
static uint16_t patternPreviewMs(VisualStateManager::LedPattern pattern)
{
  uint32_t cycle = 0;
  withPatternTables(pattern, [&cycle](const auto& mono, const auto& rgb) { cycle = s_rgbLedDriver != nullptr ? cycleMs(rgb) : cycleMs(mono); });
  return static_cast<uint16_t>(std::clamp<uint32_t>(cycle * 2, VisualStateManager::kPatternPreviewMinMs, VisualStateManager::kPatternPreviewMaxMs));
}

// Overrides and the status they report. All guarded by s_applyMutex.
static VisualStateManager::LedOverrideState s_overrides;
static std::optional<VisualStateManager::LedStatus> s_postedStatus;  // The status the last notice reported
static esp_timer_handle_t s_overrideTimer = nullptr;                 // Fires at the next override deadline

// The brightness `type` shows `signal` patterns at: the draft's while one is held, else the saved one
static uint16_t effectiveBrightness(LedType type, LedSignal signal)
{
  const auto& draft = s_overrides.Draft();
  if (draft.has_value()) {
    const auto& pair = type == LedType::Mono ? draft->mono : draft->rgb;
    if (pair.has_value()) {
      return signal == LedSignal::Critical ? pair->critical : pair->normal;
    }
  }
  return brightnessSetting(type, signal).load();
}

// `monoSolid`: with both LEDs, the mono LED stays on solid for this pattern (the hub is fully up) rather than dark.
// `showAll` ignores the LED mode, for previews and the LED test; the brightness still applies.
static void applyPattern(VisualStateManager::LedPattern pattern, bool monoSolid, bool showAll)
{
  bool gpioActive = s_monoLedDriver != nullptr;
  bool rgbActive  = s_rgbLedDriver != nullptr;

  bool critical = isCriticalPattern(pattern);
  LedMode mode  = showAll ? LedMode::All : s_ledMode.load();
  if (mode == LedMode::Off || (mode == LedMode::CriticalOnly && !critical)) {
    // Off for real: the drivers rest their hardware rather than keep writing black
    if (gpioActive) {
      s_monoLedDriver->Sleep();
    }
    if (rgbActive) {
      s_rgbLedDriver->Sleep();
    }
    return;
  }

  // Before SetPattern: the pattern task applies the brightness with each step, and SetPattern restarts the pattern,
  // so even a long solid step picks up a new brightness at once
  LedSignal signal = critical ? LedSignal::Critical : LedSignal::Normal;
  if (gpioActive) {
    s_monoLedDriver->SetBrightness(toDriverBrightness(effectiveBrightness(LedType::Mono, signal)));
  }
  if (rgbActive) {
    s_rgbLedDriver->SetBrightness16(effectiveBrightness(LedType::Rgb, signal));
  }

  withPatternTables(pattern, [&](const auto& monoPattern, const auto& rgbPattern) {
    if (gpioActive && rgbActive) {
      if (critical) {
        s_monoLedDriver->SetPattern(monoPattern);  // E-Stop and critical errors on both LEDs, at their critical brightness
      } else if (monoSolid) {
        s_monoLedDriver->SetPattern(kSolidOnPattern);
      } else {
        s_monoLedDriver->Sleep();  // The RGB LED shows the other states
      }
      s_rgbLedDriver->SetPattern(rgbPattern);
    } else if (gpioActive) {
      s_monoLedDriver->SetPattern(monoPattern);
    } else if (rgbActive) {
      s_rgbLedDriver->SetPattern(rgbPattern);
    } else {
      OS_LOGW(TAG, "Trying to update visual state, but no LED is active!");
    }
  });
}

static void applyVisualState(uint32_t flags, bool showAll = false)
{
  applyPattern(patternForFlags(flags), flags == kStatusOKMask, showAll);
}

static void applyColorPreview(const VisualStateManager::LedDisplayContent& preview)
{
  bool monoLit = !preview.only.has_value() || *preview.only == LedType::Mono;
  bool rgbLit  = !preview.only.has_value() || *preview.only == LedType::Rgb;

  if (s_monoLedDriver != nullptr) {
    s_monoLedDriver->SetBrightness(toDriverBrightness(effectiveBrightness(LedType::Mono, preview.signal)));
    bool on = monoLit && (preview.red | preview.green | preview.blue) != 0;
    if (on) {
      s_monoLedDriver->SetPattern(kSolidOnPattern);
    } else {
      s_monoLedDriver->Sleep();
    }
  }

  if (s_rgbLedDriver != nullptr) {
    if (rgbLit) {
      s_rgbLedDriver->SetBrightness16(effectiveBrightness(LedType::Rgb, preview.signal));
      const RgbLedDriver::RGBState16 color[] = {
        {preview.red, preview.green, preview.blue, 100'000}
      };
      s_rgbLedDriver->SetPattern(color);
    } else {
      s_rgbLedDriver->Sleep();
    }
  }
}

// Caller holds s_applyMutex
static void applyCurrentLocked()
{
  if (s_ledTestActive.load()) {
    return;  // Re-applied from the live flags when the test finishes
  }

  const auto& display = s_overrides.Display();
  if (display.has_value()) {
    if (display->isColor) {
      applyColorPreview(*display);
    } else {
      applyPattern(display->pattern, display->pattern == VisualStateManager::LedPattern::Connected, true);
    }
    return;
  }

  applyVisualState(s_stateFlags.load());
}

// Caller holds s_applyMutex
static VisualStateManager::LedStatus statusLocked()
{
  using namespace VisualStateManager;

  LedStatus status {
    .showing = LedShowing::Live,
    .pattern = patternForFlags(s_stateFlags.load()),
    .display = s_overrides.Info(LedOverrideLayer::Display),
    .draft   = s_overrides.Info(LedOverrideLayer::Draft),
  };

  const auto& display = s_overrides.Display();
  if (s_ledTestActive.load()) {
    status.showing = LedShowing::Test;
  } else if (display.has_value()) {
    status.showing = display->isColor ? LedShowing::Color : LedShowing::Pattern;
    if (!display->isColor) {
      status.pattern = display->pattern;
    }
  } else {
    LedMode mode = s_ledMode.load();
    if (mode == LedMode::Off || (mode == LedMode::CriticalOnly && !isCriticalPattern(status.pattern))) {
      status.showing = LedShowing::Hidden;
    }
  }
  return status;
}

// Caller holds s_applyMutex. Points the timer at the next override deadline.
static void rearmOverrideTimerLocked()
{
  if (s_overrideTimer == nullptr) {
    return;
  }
  esp_timer_stop(s_overrideTimer);  // ESP_ERR_INVALID_STATE when not running: fine
  auto deadline = s_overrides.NextDeadlineUs();
  if (!deadline.has_value()) {
    return;
  }
  int64_t waitUs = std::max<int64_t>(*deadline - esp_timer_get_time(), 1000);
  esp_err_t err  = esp_timer_start_once(s_overrideTimer, static_cast<uint64_t>(waitUs));
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to start the LED override timer: %s", esp_err_to_name(err));
  }
}

namespace {
  // Notices gathered under s_applyMutex, posted once it is released: a portal handler reads the status back
  struct PendingNotices {
    std::array<VisualStateManager::LedStatusNotice, 4> items;
    std::size_t count = 0;

    void ended(const VisualStateManager::LedOverrideEnded& ended) { add({.kind = VisualStateManager::LedStatusNotice::Kind::Ended, .layer = ended.layer, .reason = ended.reason, .error = {}, .owner = ended.owner, .seq = ended.seq}); }
    void ended(const std::optional<VisualStateManager::LedOverrideEnded>& ended)
    {
      if (ended.has_value()) {
        this->ended(*ended);
      }
    }
    void failed(uint8_t owner, uint16_t seq, VisualStateManager::LedRequestError error) { add({.kind = VisualStateManager::LedStatusNotice::Kind::Failed, .layer = {}, .reason = {}, .error = error, .owner = owner, .seq = seq}); }
    void add(const VisualStateManager::LedStatusNotice& notice)
    {
      if (count < items.size()) {
        items[count++] = notice;
      }
    }
  };
}  // namespace

static void postNotices(const PendingNotices& notices)
{
  for (std::size_t i = 0; i < notices.count; ++i) {
    esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_LED_STATUS_CHANGED, &notices.items[i], sizeof(notices.items[i]), pdMS_TO_TICKS(10));
    if (err != ESP_OK) {
      OS_LOGW(TAG, "Failed to post the LED status: %s", esp_err_to_name(err));
    }
  }
}

// Every change to the LEDs goes through here: `change` edits the state under s_applyMutex, then the LEDs, the override
// timer and the portals are brought up to date. A status change with nothing else to report posts StatusChanged.
template<typename F>
static void updateLeds(F&& change)
{
  PendingNotices notices;
  {
    OpenShock::ScopedLock lock__(&s_applyMutex);
    change(notices);
    applyCurrentLocked();
    rearmOverrideTimerLocked();

    auto status    = statusLocked();
    bool changed   = s_postedStatus != status;
    s_postedStatus = status;
    if (changed && notices.count == 0) {
      notices.add({.kind = VisualStateManager::LedStatusNotice::Kind::StatusChanged, .layer = {}, .reason = {}, .error = {}, .owner = 0, .seq = 0});
    }
  }
  postNotices(notices);
}

static void updateVisualState()
{
  updateLeds([](PendingNotices&) { });
}

// esp_timer callback (esp_timer task, small stack): only hands the expiry to the event loop, which does the LED work.
// The expiry is a sweep of whatever is due, so a late or extra tick is harmless; a lost one is retried.
static void handleOverrideTimer(void* arg)
{
  (void)arg;
  if (esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_LED_OVERRIDE_DUE, nullptr, 0, 0) != ESP_OK) {
    esp_timer_start_once(s_overrideTimer, 50'000);
  }
}

static void expireOverrides()
{
  updateLeds([](PendingNotices& notices) { s_overrides.Expire(esp_timer_get_time(), [&notices](const VisualStateManager::LedOverrideEnded& ended) { notices.ended(ended); }); });
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
    case OPENSHOCK_EVENT_LED_OVERRIDE_DUE:
      expireOverrides();
      return;
    case OPENSHOCK_EVENT_DEVICE_ACTIVITY:
    case OPENSHOCK_EVENT_LED_CONFIG_CHANGED:  // Posted from here
    case OPENSHOCK_EVENT_LED_STATUS_CHANGED:  // Posted from here
      return;
    default:
      OS_LOGW(TAG, "Received unknown event ID: %d", static_cast<int>(event_id));
      return;
  }

  if (changed) {
    updateVisualState();
  }
}

// Copies the `led` section into the atomics applyVisualState reads
static void loadLedConfig()
{
  const Config::LedConfig ledConfig = Config::GetLedConfig();
  s_ledMode.store(ledConfig.mode);
  for (LedType type : {LedType::Mono, LedType::Rgb}) {
    for (LedSignal signal : {LedSignal::Critical, LedSignal::Normal}) {
      brightnessSetting(type, signal).store(ledConfig.Brightness(type, signal));
    }
  }
}

bool VisualStateManager::Init()
{
  bool ledActive = false;

  const BoardDescriptor& board = Board::Get();

  // Before the drivers, so they start at the configured brightness
  loadLedConfig();

  // The pins come from the stamped descriptor, so they are data, not something the build checked. A pin this
  // chip cannot drive leaves that LED off rather than failing Init: that would restart-loop a normal boot and
  // roll back an OTA boot over an indicator. ValidateBoardDescriptor() has already logged the specifics.
  if (board.ledGpio != GPIO_NUM_NC) {
    if (!OpenShock::IsValidOutputPin(board.ledGpio)) {
      OS_LOGW(TAG, "Built-in LED pin %hhi is not usable on this board, leaving the LED disabled", board.ledGpio);
    } else {
      s_monoLedDriver = std::make_unique<MonoLedDriver>(static_cast<gpio_num_t>(board.ledGpio), toDriverBrightness(s_monoNormalBrightness.load()));
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
      s_rgbLedDriver = std::make_unique<RgbLedDriver>(static_cast<gpio_num_t>(board.ws2812bGpio), s_rgbNormalBrightness.load());
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

  const esp_timer_create_args_t overrideTimerArgs = {
    .callback              = handleOverrideTimer,
    .arg                   = nullptr,
    .dispatch_method       = ESP_TIMER_TASK,
    .name                  = "LedOverride",
    .skip_unhandled_events = true,
  };
  esp_err_t err = esp_timer_create(&overrideTimerArgs, &s_overrideTimer);
  if (err != ESP_OK) {
    // Overrides only (they are turned down without it); the LEDs still show the hub's state
    OS_LOGE(TAG, "Failed to create the LED override timer: %s", esp_err_to_name(err));
    s_overrideTimer = nullptr;
  }

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
  return UpdateLedConfig([&](Config::LedConfig& led) {
    if (led.mode == mode) {
      return false;
    }
    led.mode = mode;
    return true;
  });
}

uint16_t VisualStateManager::GetLedBrightness(LedType type, LedSignal signal)
{
  return brightnessSetting(type, signal).load();
}

bool VisualStateManager::SetLedBrightness(LedType type, LedSignal signal, uint16_t value)
{
  return UpdateLedConfig([&](Config::LedConfig& led) {
    uint16_t& brightness = led.Brightness(type, signal);
    if (brightness == value) {
      return false;
    }
    brightness = value;  // The section caps it (LedConfig::Normalize)
    return true;
  });
}

bool VisualStateManager::UpdateLedConfig(Util::FunctionRef<bool(Config::LedConfig&)> update)
{
  if (!Config::UpdateLedConfig(update)) {
    OS_LOGE(TAG, "Failed to save the LED settings to config");
    return false;
  }

  // Under s_applyMutex so two updates can't store their snapshots out of order
  updateLeds([](PendingNotices&) {
    uint16_t rgbBefore[] = {s_rgbCriticalBrightness.load(), s_rgbNormalBrightness.load()};
    loadLedConfig();
    uint16_t rgbAfter[] = {s_rgbCriticalBrightness.load(), s_rgbNormalBrightness.load()};
    for (std::size_t i = 0; i < std::size(rgbAfter); ++i) {
      if (s_rgbLedDriver != nullptr && rgbAfter[i] > VisualStateManager::kRgbBrightnessWarn && rgbAfter[i] != rgbBefore[i]) {
        OS_LOGW(TAG, "RGB LED %s brightness set to %u/65535: above %u a WS2812B is glaring and runs warm", i == 0 ? "critical" : "normal", rgbAfter[i], VisualStateManager::kRgbBrightnessWarn);
      }
    }
  });

  // Tells open portals, which otherwise only get the settings on connect. Posted after s_applyMutex is released: a
  // handler reads the settings back.
  esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_LED_CONFIG_CHANGED, nullptr, 0, pdMS_TO_TICKS(10));
  if (err != ESP_OK) {
    OS_LOGW(TAG, "Failed to post the LED settings change: %s", esp_err_to_name(err));
  }
  return true;
}

bool VisualStateManager::HasLed(LedType type)
{
  return type == LedType::Mono ? s_monoLedDriver != nullptr : s_rgbLedDriver != nullptr;
}

VisualStateManager::LedStatus VisualStateManager::GetLedStatus()
{
  OpenShock::ScopedLock lock__(&s_applyMutex);
  return statusLocked();
}

// Caller holds s_applyMutex. Whether an override may start now; reports why not.
static bool canOverrideLocked(PendingNotices& notices, uint8_t owner, uint16_t seq)
{
  using VisualStateManager::LedRequestError;
  if (!VisualStateManager::HasAnyLed() || s_overrideTimer == nullptr) {
    notices.failed(owner, seq, LedRequestError::NoLed);  // No timer: nothing would end it
    return false;
  }
  if (s_ledTestActive.load()) {
    notices.failed(owner, seq, LedRequestError::LedTestRunning);
    return false;
  }
  return true;
}

void VisualStateManager::PreviewLedPattern(uint8_t owner, uint16_t seq, LedPattern pattern)
{
  updateLeds([&](PendingNotices& notices) {
    if (!canOverrideLocked(notices, owner, seq)) return;
    LedDisplayContent content {.isColor = false, .pattern = pattern, .red = 0, .green = 0, .blue = 0, .only = std::nullopt, .signal = LedSignal::Normal};
    notices.ended(s_overrides.SetDisplay(owner, seq, content, patternPreviewMs(pattern), esp_timer_get_time()));
  });
}

void VisualStateManager::PreviewLedColor(uint8_t owner, uint16_t seq, uint16_t red, uint16_t green, uint16_t blue, std::optional<LedType> only, LedSignal signal)
{
  updateLeds([&](PendingNotices& notices) {
    if (!canOverrideLocked(notices, owner, seq)) return;
    LedDisplayContent content {.isColor = true, .pattern = LedPattern::WiFiDisconnected, .red = red, .green = green, .blue = blue, .only = only, .signal = signal};
    notices.ended(s_overrides.SetDisplay(owner, seq, content, kColorPreviewTimeoutMs, esp_timer_get_time()));
  });
}

void VisualStateManager::SetLedDraft(uint8_t owner, uint16_t seq, const LedDraftBrightness& requested)
{
  // Only the LEDs the board has; the portal never shows the others
  LedDraftBrightness draft = requested;
  if (!HasLed(LedType::Mono)) draft.mono.reset();
  if (!HasLed(LedType::Rgb)) draft.rgb.reset();

  updateLeds([&](PendingNotices& notices) {
    if (!canOverrideLocked(notices, owner, seq)) return;
    if (!draft.mono.has_value() && !draft.rgb.has_value()) {
      notices.failed(owner, seq, LedRequestError::NoLed);
      return;
    }
    notices.ended(s_overrides.SetDraft(owner, seq, draft, kDraftBrightnessTimeoutMs, esp_timer_get_time()));
  });
}

void VisualStateManager::StopLedOverride(uint8_t owner, uint16_t seq, LedOverrideLayer layer)
{
  updateLeds([&](PendingNotices& notices) {
    if (!s_overrides.IsHeldBy(layer, owner)) {
      notices.failed(owner, seq, LedRequestError::NotHeld);
      return;
    }
    notices.ended(s_overrides.End(layer, LedOverrideEnd::Stopped, seq));
  });
}

void VisualStateManager::CommitLedDraft(uint8_t owner, uint16_t seq)
{
  std::optional<LedDraftBrightness> draft;
  {
    OpenShock::ScopedLock lock__(&s_applyMutex);
    if (s_overrides.IsHeldBy(LedOverrideLayer::Draft, owner)) {
      draft = s_overrides.Draft();
    }
  }
  if (!draft.has_value()) {
    ReportLedRequestFailed(owner, seq, LedRequestError::NotHeld);
    return;
  }

  // The draft stays applied while saving, so the LEDs don't flicker back to the old brightness in between
  bool saved = UpdateLedConfig([&](Config::LedConfig& led) {
    bool changed = false;
    auto apply   = [&](LedType type, const std::optional<LedDraftBrightness::Pair>& pair) {
      if (!pair.has_value()) return;
      for (LedSignal signal : {LedSignal::Critical, LedSignal::Normal}) {
        uint16_t value  = signal == LedSignal::Critical ? pair->critical : pair->normal;
        uint16_t& field = led.Brightness(type, signal);
        if (field != value) {
          field   = value;  // The section caps it (LedConfig::Normalize)
          changed = true;
        }
      }
    };
    apply(LedType::Mono, draft->mono);
    apply(LedType::Rgb, draft->rgb);
    return changed;
  });
  if (!saved) {
    ReportLedRequestFailed(owner, seq, LedRequestError::SaveFailed);
    return;
  }

  // Ends whatever draft the owner holds by now: anything it sent while saving starts a new one with its next message
  updateLeds([&](PendingNotices& notices) {
    if (s_overrides.IsHeldBy(LedOverrideLayer::Draft, owner)) {
      notices.ended(s_overrides.End(LedOverrideLayer::Draft, LedOverrideEnd::Committed, seq));
    }
  });
}

void VisualStateManager::SetLedModeFor(uint8_t owner, uint16_t seq, LedMode mode)
{
  if (!HasAnyLed()) {
    ReportLedRequestFailed(owner, seq, LedRequestError::NoLed);
    return;
  }
  if (!SetLedMode(mode)) {
    ReportLedRequestFailed(owner, seq, LedRequestError::SaveFailed);
  }
}

void VisualStateManager::ReportLedRequestFailed(uint8_t owner, uint16_t seq, LedRequestError error)
{
  PendingNotices notices;
  notices.failed(owner, seq, error);
  postNotices(notices);
}

void VisualStateManager::ReleaseLedOverrides(uint8_t owner)
{
  updateLeds([&](PendingNotices& notices) {
    for (LedOverrideLayer layer : {LedOverrideLayer::Display, LedOverrideLayer::Draft}) {
      if (s_overrides.IsHeldBy(layer, owner)) {
        notices.ended(s_overrides.End(layer, LedOverrideEnd::OwnerLeft));
      }
    }
  });
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
  // Under s_applyMutex so an update already in flight finishes before the test starts. Overrides end, and new ones are
  // turned down until the test is done.
  bool alreadyRunning = false;
  updateLeds([&alreadyRunning](PendingNotices& notices) {
    if (s_ledTestActive.exchange(true)) {
      alreadyRunning = true;
      return;
    }
    for (VisualStateManager::LedOverrideLayer layer : {VisualStateManager::LedOverrideLayer::Display, VisualStateManager::LedOverrideLayer::Draft}) {
      notices.ended(s_overrides.End(layer, VisualStateManager::LedOverrideEnd::LedTest));
    }
  });
  if (alreadyRunning) {
    OS_LOGW(TAG, "LED test already running");
    return;
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
  updateLeds([](PendingNotices&) { s_ledTestActive.store(false); });
}
