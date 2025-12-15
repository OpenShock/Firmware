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

#include <cstdint>

using namespace OpenShock;

const uint32_t k_estopHoldToClearTime = 5000;
const uint32_t k_estopUpdateRate      = 5;   // 200 Hz
const uint32_t k_estopCheckCount      = 13;  // 65 ms at 200 Hz
const uint16_t k_estopCheckMask       = 0xFFFF >> ((sizeof(uint16_t) * 8) - k_estopCheckCount);

static OpenShock::SimpleMutex s_estopMutex = {};
static gpio_num_t s_estopPin               = GPIO_NUM_NC;
static TaskHandle_t s_estopTask            = nullptr;

static EStopState s_estopState         = EStopState::Idle;
static EStopState s_lastPublishedState = EStopState::Idle;
static bool       s_estopActive        = false;
static int64_t    s_estopActivatedAt   = 0;

static volatile bool s_externallyTriggered = false;

static bool s_estopInitialized = false;

// Forward declarations
static bool estopmgr_setestopenabled(bool enabled);
static bool _setEStopPinImpl(gpio_num_t pin);

static void estopmanager_updateexternals(bool isActive, bool isAwaitingRelease)
{
  // Parameters are here for future extension (e.g. different event payload types).
  (void)isActive;
  (void)isAwaitingRelease;

  if (s_estopState == s_lastPublishedState) {
    return; // No state change → no event
  }

  s_lastPublishedState = s_estopState;

  // Post the current state as the event payload
  ESP_ERROR_CHECK(esp_event_post(OPENSHOCK_EVENTS,
                                 OPENSHOCK_EVENT_ESTOP_STATE_CHANGED,
                                 &s_estopState,
                                 sizeof(s_estopState),
                                 portMAX_DELAY));
}

// Samples the estop at a fixed rate and updates internal state + events
static void estopmgr_checkertask(void* pvParameters)
{
  (void)pvParameters;

  // Bit history of samples, 0 is pressed, 1 is released.
  uint16_t history = 0xFFFF;

  int64_t deactivatesAt = 0;

  // Debounced button state: true == pressed, false == released
  bool lastBtnState = false;

  for (;;) {
    // Sleep for the update rate
    vTaskDelay(pdMS_TO_TICKS(k_estopUpdateRate));

    // Get current time
    int64_t now = OpenShock::millis();

    bool btnState;

    // Handle external trigger: forcibly set the E-Stop active.
    if (s_externallyTriggered) {
      s_externallyTriggered = false;

      s_estopActive      = true;
      s_estopActivatedAt = now;
      s_estopState       = EStopState::Active;

      estopmanager_updateexternals(s_estopActive, false);

      // Do not modify history/lastBtnState here; allow hardware to take over
      // on subsequent iterations.
      continue;
    }

    // Sample the EStop input
    history = static_cast<uint16_t>((history << 1) | gpio_get_level(s_estopPin));

    // Debounce:
    // If all recent bits (k_estopCheckCount) are 1 -> fully released.
    // If any bit is 0 -> we consider it pressed (or bouncing toward pressed).
    btnState = (history & k_estopCheckMask) != k_estopCheckMask;  // true == pressed

    // If the debounced state hasn't changed, we only care about timing transitions.
    if (btnState == lastBtnState) {
      if (s_estopState == EStopState::ActiveClearing && now > deactivatesAt) {
        s_estopState = EStopState::AwaitingRelease;
        estopmanager_updateexternals(s_estopActive, true);
      }
      continue;
    }

    // State changed: update lastBtnState and run the state machine.
    lastBtnState = btnState;

    switch (s_estopState) {
      case EStopState::Idle:
        if (btnState) {  // pressed
          s_estopState       = EStopState::Active;
          s_estopActive      = true;
          s_estopActivatedAt = now;
        }
        break;

      case EStopState::Active:
        if (btnState) {  // still pressed -> start hold-to-clear timer
          s_estopState  = EStopState::ActiveClearing;
          deactivatesAt = now + k_estopHoldToClearTime;
        }
        break;

      case EStopState::ActiveClearing:
        if (!btnState) {  // released before hold time -> go back to Active
          s_estopState = EStopState::Active;
        } else if (now > deactivatesAt) {
          s_estopState = EStopState::AwaitingRelease;
        }
        break;

      case EStopState::AwaitingRelease:
        if (!btnState) {  // fully released -> clear E-Stop
          s_estopState  = EStopState::Idle;
          s_estopActive = false;
        }
        break;

      default:
        // Should never happen
        break;
    }

    estopmanager_updateexternals(s_estopActive, s_estopState == EStopState::AwaitingRelease);
  }
}

static bool estopmgr_setestopenabled(bool enabled)
{
  if (enabled) {
    if (s_estopTask == nullptr) {
      if (TaskUtils::TaskCreateUniversal(estopmgr_checkertask, TAG, 4096, nullptr, 5, &s_estopTask, 1) != pdPASS) {  // TODO: Profile stack size and set priority
        OS_LOGE(TAG, "Failed to create EStop event handler task");
        s_estopTask = nullptr;
        return false;
      }
    }
  } else {
    if (s_estopTask != nullptr) {
      vTaskDelete(s_estopTask);
      s_estopTask = nullptr;
    }
  }

  return true;
}

static bool _setEStopPinImpl(gpio_num_t pin)
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
    if (!estopmgr_setestopenabled(false)) {
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
    if (!estopmgr_setestopenabled(true)) {
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

  if (!_setEStopPinImpl(cfg.gpioPin)) {
    OS_LOGE(TAG, "Failed to set EStop pin");
    return false;
  }

  if (!estopmgr_setestopenabled(cfg.enabled)) {
    OS_LOGE(TAG, "Failed to create EStop event handler task");
    return false;
  }

  // Ensure known initial state
  s_estopState       = EStopState::Idle;
  s_estopActive      = false;
  s_estopActivatedAt = 0;

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
    if (!_setEStopPinImpl(pin)) {
      OS_LOGE(TAG, "Failed to set EStop pin");
      return false;
    }
  }

  return estopmgr_setestopenabled(enabled);
}

bool EStopManager::SetEStopPin(gpio_num_t pin)
{
  OpenShock::ScopedLock lock__(&s_estopMutex);

  return _setEStopPinImpl(pin);
}

bool EStopManager::IsEStopped()
{
  return s_estopActive;
}

int64_t EStopManager::LastEStopped()
{
  return s_estopActivatedAt;
}

void EStopManager::Trigger()
{
  // This will be picked up by the checker task and converted into an
  // E-Stop activation (s_estopState = Active, s_estopActive = true, etc.)
  s_externallyTriggered = true;
}
