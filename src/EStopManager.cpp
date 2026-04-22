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
static TaskHandle_t s_estopTask = nullptr;
static gpio_num_t s_estopPin    = GPIO_NUM_NC;  // Passed to task via pointer argument

// Wrapped in atomics as they're read (or set via public methods) by Tasks potentially running on other cores.
static std::atomic<int64_t> s_estopActivatedAt = 0; // When == 0, EStop not active. When != 0, EStop is active.
static std::atomic<bool> s_externallyTriggered = false;
static std::atomic<bool> s_killEStopManagerRequested = false;

static bool s_estopInitialized = false;

static void estopmgr_publishState(EStopState state, EStopState& lastState)
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
static void estopmgr_managerTask(void* pvParameters)
{
  // Pin is being passed as a pointer, cast it back to gpio number type to get pin value
  gpio_num_t estopPin = static_cast<gpio_num_t>(reinterpret_cast<intptr_t>(pvParameters));

  // Ensure known initial state
  s_estopActivatedAt.store(0, std::memory_order_relaxed);

  EStopState state              = EStopState::Idle;
  EStopState lastPublishedState = EStopState::Idle;

  uint16_t history = 0xFFFF;  // Bit history of samples, 0 is pressed

  int64_t deactivatesAt = 0;
  
  // Rearm grace state
  int64_t rearmAt   = 0;
  bool rearmBlocked = false;

  // Debounced button state: true == pressed, false == released
  bool lastBtnState = false;

  // Check if killing manager was requested, continue looping otherwise
  while (!s_killEStopManagerRequested.load(std::memory_order_relaxed)) {

    // Sleep for the update rate
    vTaskDelay(pdMS_TO_TICKS(k_estopUpdateRate));

    // Get current time
    int64_t now = OpenShock::millis();

    // Handle external trigger: forcibly set the E-Stop active.
    if (s_externallyTriggered.exchange(false, std::memory_order_relaxed)) {
      int64_t zero = 0;
      s_estopActivatedAt.compare_exchange_strong(zero, now, std::memory_order_relaxed);
      
      state        = EStopState::Active;
      rearmBlocked = false;

      estopmgr_publishState(state, lastPublishedState);

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

    estopmgr_publishState(state, lastPublishedState);
  }

  // Broke out of main loop, set global variables to Idle state.
  estopmgr_publishState(EStopState::Idle, lastPublishedState);
  s_estopActivatedAt.store(0, std::memory_order_relaxed);

  vTaskDelete(nullptr);
}

static bool estopmgr_setPinImpl(gpio_num_t pin)
{
  esp_err_t err;

  if (!OpenShock::IsValidInputPin(pin)) {
    OS_LOGE(TAG, "Invalid EStop pin: %hhi", static_cast<int8_t>(pin));
    return false;
  }

  if (s_estopPin == pin) {
    return true;
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

  return true;
}

static bool estopmgr_taskStart()
{
  if (s_estopTask != nullptr) {
    OS_LOGW(TAG, "Tried to enable EStop manager, but was already running");
    return true;
  }

  if (s_estopPin == GPIO_NUM_NC) {
    gpio_num_t pin;
    if (!OpenShock::Config::GetEStopGpioPin(pin)) {
      OS_LOGE(TAG, "Failed to get EStop pin from config");
      return false;
    }

    if (pin == GPIO_NUM_NC) {
      OS_LOGW(TAG, "No valid pin is defined, refusing to start task");
      return false;
    }
    
    if (!estopmgr_setPinImpl(pin)) {
      OS_LOGE(TAG, "Failed to set EStop pin");
      return false;
    }
  }

  s_killEStopManagerRequested.store(false, std::memory_order_relaxed);

  // Tiny hack;
  // We are passing pin as a pointer, the pointer memory address being the value of the pin.
  // 
  // A pointer after all is just a integer with a special meaning, arg pointer isn't being used for anything so we can use it for this.
  // 
  // This also proves to be safer than using atomics for this since we know for sure that the pin we intended the task to run with
  // will not change between creating the task and it freezing its local copy of the value.
  // 
  // This enables us to use no allocations or atomic operations
  static_assert(sizeof(void*) >= sizeof(gpio_num_t), "void* is smaller than gpio_num_t, value embedding trick wont work"); // Just to be safe
  void* argPtr = reinterpret_cast<void*>(static_cast<intptr_t>(s_estopPin));

  if (TaskUtils::TaskCreateUniversal(estopmgr_managerTask, TAG, 4096, argPtr, 5, &s_estopTask, 1) != pdPASS) {  // TODO: Profile stack size and set priority
    OS_LOGE(TAG, "Failed to create EStop event handler task");
    s_estopTask = nullptr;
    return false;
  }

  return true;
}

static bool estopmgr_taskStop()
{
  if (s_estopTask == nullptr) {
    OS_LOGW(TAG, "Tried to kill EStop manager, but was not running");
    return true;
  }

  s_killEStopManagerRequested.store(true, std::memory_order_relaxed);
  
  TaskUtils::StopTask(s_estopTask, TAG, "EStop task");
  s_estopTask = nullptr;

  // Disable E-Stop after task has stopped to ensure that the task didn't get it stuck in enabled state
  s_estopActivatedAt.store(0, std::memory_order_relaxed);

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

  if (!cfg.enabled) {
    return true;
  }

  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (!estopmgr_setPinImpl(cfg.gpioPin)) {
    OS_LOGE(TAG, "Failed to set EStop pin");
    return false;
  }

  return estopmgr_taskStart();
}

bool EStopManager::SetEStopEnabled(bool enabled)
{
  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (enabled) {
    return estopmgr_taskStart();
  } else {
    return estopmgr_taskStop();
  }
}

bool EStopManager::SetEStopPin(gpio_num_t pin)
{
  OpenShock::ScopedLock lock__(&s_estopMutex);

  // Check pin validity before stopping possibly running task
  if (!OpenShock::IsValidInputPin(pin)) {
    OS_LOGE(TAG, "Invalid EStop pin: %hhi", static_cast<int8_t>(pin));
    return false;
  }

  if (s_estopPin == pin) {
    return true;
  }

  bool wasRunning = s_estopTask != nullptr;
  if (wasRunning && !estopmgr_taskStop()) {
    return false;
  }

  if (!estopmgr_setPinImpl(pin)) {
    return false;
  }

  if (wasRunning && !estopmgr_taskStart()) {
    return false;
  }

  return true;
}

bool EStopManager::IsEStopped()
{
  return EStopManager::LastEStopped() != 0;
}

int64_t EStopManager::LastEStopped()
{
  return s_estopActivatedAt.load(std::memory_order_relaxed);
}

void EStopManager::SoftwareTrigger()
{
  // This will be picked up by the checker task and lead to an E-Stop activation
  s_externallyTriggered.store(true, std::memory_order_relaxed);
}
