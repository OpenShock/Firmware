#include <freertos/FreeRTOS.h>

#include "estop/EStopManager.h"

const char* const TAG = "EStopManager";

#include "Chipset.h"
#include "config/Config.h"
#include "Core.h"
#include "events/Events.h"
#include "Logging.h"
#include "SimpleMutex.h"
#include "util/TaskUtils.h"

#include <driver/gpio.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <atomic>
#include <cstdint>

using namespace OpenShock;

const uint32_t k_estopHoldToClearTime = 5000;
const uint32_t k_estopUpdateRate      = 5;   // 200 Hz
const uint32_t k_estopCheckCount      = 13;  // 65 ms at 200 Hz
const uint16_t k_estopCheckMask       = 0xFFFF >> ((sizeof(uint16_t) * 8) - k_estopCheckCount); // Mask to check only last k_estopCheckCount bits within history

// Grace period after deactivation (prevents immediate re-trigger on release bounce/EMI)
const uint32_t k_estopRearmGraceTime = 250;  // tune as needed

static OpenShock::SimpleMutex s_estopMutex = {};
// Guarded via Mutex
static TaskHandle_t s_estopTask            = nullptr;

// Wrapped in atomics as they're read (or set via public methods) by Tasks potentially running on other cores.
static std::atomic<gpio_num_t> s_estopPin  = GPIO_NUM_NC;
static std::atomic<int64_t> s_estopActivatedAt = 0; // When == 0, EStop not active. When != 0, EStop is active.
static std::atomic<bool> s_externallyTriggered = false;
static std::atomic<bool> s_killEStopManagerRequested = false;

static bool s_estopInitialized = false;

static void estopmgr_PublishState(EStopState state, EStopState& lastState)
{
  if (state == lastState) {
    return;  // No state change -> no event
  }
  
  // Post the current state as the event payload
  esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_ESTOP_STATE_CHANGED, &state, sizeof(state), pdMS_TO_TICKS(750));

  if (err == ESP_OK) {
    lastState = state;
  } else {
    OS_LOGE(TAG, "Failed to publish EStop event");
  }
}

// Samples the estop at a fixed rate and updates internal state + events
static void estopmgr_ManagerTask(void* pvParameters)
{
  // Ensure known initial state
  s_estopActivatedAt.store(0, std::memory_order_relaxed);

  gpio_num_t estopPin = s_estopPin.load(std::memory_order_relaxed);

  EStopState state              = EStopState::Idle;
  EStopState lastPublishedState = EStopState::Idle;

  uint16_t history = 0xFFFF;  // Bit history of samples, 0 is pressed

  int64_t deactivatesAt = 0;
  
  // Rearm grace state
  int64_t rearmAt   = 0;
  bool rearmBlocked = false;

  // Debounced button state: true == pressed, false == released
  bool lastBtnState = false;

  for (;;) {
    // Check if killing manager was requested
    if (s_killEStopManagerRequested.load(std::memory_order_relaxed)) break;

    // Sleep for the update rate
    vTaskDelay(pdMS_TO_TICKS(k_estopUpdateRate));

    // Get current time
    int64_t now = OpenShock::millis();

    // Handle external trigger: forcibly set the E-Stop active.
    if (s_externallyTriggered.exchange(false, std::memory_order_acquire)) {
      if (s_estopActivatedAt.load(std::memory_order_acquire) == 0) {
        s_estopActivatedAt.store(now, std::memory_order_release);
      }
      
      state        = EStopState::Active;
      rearmBlocked = false;

      estopmgr_PublishState(state, lastPublishedState);

      // Do not modify history/lastBtnState here; rely on physical button state
      // on subsequent iterations.
      continue;
    }

    // Sample the EStop input
    history = static_cast<uint16_t>((history << 1) | gpio_get_level(estopPin));

    // Debounce:
    // If all recent bits are 1 -> fully released.
    // If any bit is 0 -> pressed (or bouncing toward pressed).
    bool btnState    = (history & k_estopCheckMask) != k_estopCheckMask;  // true == pressed
    bool pressedEdge = (btnState && !lastBtnState);
    lastBtnState     = btnState;

    switch (state) {
      case EStopState::Idle:
        // Rearm grace: after clearing, ignore presses for a short window.
        // After the window ends, require a released state before re-arming.
        if (rearmBlocked) {
          if (now < rearmAt) {
            // Still in grace window: ignore any press. Track input to avoid phantom edges later.
            break;
          }

          // Grace window ended: only re-arm once we see released.
          rearmBlocked = false;
        }

        if (btnState) {
          state = EStopState::Active;
          s_estopActivatedAt.store(now, std::memory_order_relaxed);
        }
        break;

      case EStopState::Active:
        // Once active, if the input gets pressed, start hold-to-clear timing.
        if (pressedEdge) {
          state         = EStopState::ActiveClearing;
          deactivatesAt = now + k_estopHoldToClearTime;
        }
        break;

      case EStopState::ActiveClearing:
        if (!btnState) {  // released before hold time -> go back to Active
          state = EStopState::Active;
        } else if (now >= deactivatesAt) {
          // Hold complete -> now wait for release edge to fully clear
          state = EStopState::AwaitingRelease;
        }
        break;

      case EStopState::AwaitingRelease:
        if (!btnState) {  // fully released -> clear E-Stop
          state = EStopState::Idle;
          s_estopActivatedAt.store(0, std::memory_order_relaxed);

          // Start grace period to prevent immediate re-trigger.
          rearmBlocked = true;
          rearmAt      = now + k_estopRearmGraceTime;
        }
        break;

      default:
        // Should never happen
        break;
    }

    estopmgr_PublishState(state, lastPublishedState);
  }

  // Broke out of main loop, set global variables to Idle state.
  estopmgr_PublishState(EStopState::Idle, lastPublishedState);
  s_estopActivatedAt.store(0, std::memory_order_relaxed);

  vTaskDelete(nullptr);
}

static bool estopmgr_setEStopEnabled(bool enabled)
{
  if (enabled) {
    if (s_estopTask == nullptr) {
      s_killEStopManagerRequested.store(false, std::memory_order_acquire);
      if (TaskUtils::TaskCreateUniversal(estopmgr_ManagerTask, TAG, 4096, nullptr, 5, &s_estopTask, 1) != pdPASS) {  // TODO: Profile stack size and set priority
        OS_LOGE(TAG, "Failed to create EStop event handler task");
        s_estopTask = nullptr;
        return false;
      }
    } else {
      OS_LOGW(TAG, "Tried to enable EStop manager, but was already running");
    }
  } else {
    if (s_estopTask != nullptr) {
      s_killEStopManagerRequested.store(true, std::memory_order_acquire);
      s_estopActivatedAt.store(0, std::memory_order_relaxed);
      
      TaskUtils::StopTask(s_estopTask, TAG, "EStop task");
      s_estopTask = nullptr;
    } else {
      OS_LOGW(TAG, "Tried to kill EStop manager, but was not running");
    }
  }

  return true;
}

static bool estopmgr_setPinImpl(gpio_num_t pin)
{
  esp_err_t err;

  if (s_estopPin == pin) {
    return true;
  }

  if (!OpenShock::IsValidInputPin(pin)) {
    OS_LOGE(TAG, "Invalid EStop pin: %hhi", static_cast<int8_t>(pin));
    return false;
  }

  bool wasRunning = s_estopTask != nullptr;
  if (wasRunning) {
    if (!estopmgr_setEStopEnabled(false)) {
      OS_LOGE(TAG, "Failed to disable EStop event handler task");
      return false;
    }
  }

  // Configure the new pin
  gpio_config_t io_conf = {
    .pin_bit_mask = 1ULL << pin,
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_DISABLE,
  };

  err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to configure EStop pin");
    return false;
  }

  gpio_num_t oldPin = s_estopPin;

  // Set the new pin
  s_estopPin = pin;

  if (oldPin != GPIO_NUM_NC) {
    // Reset the old pin
    err = gpio_reset_pin(oldPin);
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to reset old EStop pin");
      return false;
    }
  }

  if (wasRunning) {
    if (!estopmgr_setEStopEnabled(true)) {
      OS_LOGE(TAG, "Failed to re-enable EStop event handler task");
      return false;
    }
  }

  return true;
}

bool EStopManager::Init()
{
  if (s_estopInitialized) {
    return true;
  }
  s_estopInitialized = true;

  Config::EStopConfig cfg;
  if (!OpenShock::Config::GetEStop(cfg)) {
    OS_LOGE(TAG, "Failed to get EStop pin from config");
    return false;
  }

  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (!estopmgr_setPinImpl(cfg.gpioPin)) {
    OS_LOGE(TAG, "Failed to set EStop pin");
    return false;
  }

  if (!estopmgr_setEStopEnabled(cfg.enabled)) {
    OS_LOGE(TAG, "Failed to create EStop event handler task");
    return false;
  }

  return true;
}

bool EStopManager::SetEStopEnabled(bool enabled)
{
  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (s_estopPin == GPIO_NUM_NC) {
    gpio_num_t pin;
    if (!OpenShock::Config::GetEStopGpioPin(pin)) {
      OS_LOGE(TAG, "Failed to get EStop pin from config");
      return false;
    }
    if (!estopmgr_setPinImpl(pin)) {
      OS_LOGE(TAG, "Failed to set EStop pin");
      return false;
    }
  }

  return estopmgr_setEStopEnabled(enabled);
}

bool EStopManager::SetEStopPin(gpio_num_t pin)
{
  OpenShock::ScopedLock lock__(&s_estopMutex);

  return estopmgr_setPinImpl(pin);
}

bool EStopManager::IsEStopped()
{
  return EStopManager::LastEStopped() != 0;
}

int64_t EStopManager::LastEStopped()
{
  return s_estopActivatedAt.load(std::memory_order_acquire);
}

void EStopManager::SoftwareTrigger()
{
  // This will be picked up by the checker task and lead to an E-Stop activation
  s_externallyTriggered.store(true, std::memory_order_relaxed);
}
