#include <freertos/FreeRTOS.h>

#include "estop/EStopManager.h"

const char* const TAG = "EStopManager";

#include "Chipset.h"
#include "config/Config.h"
#include "estop/EStopStateMachine.h"
#include "events/Events.h"
#include "Logging.h"
#include "SimpleMutex.h"
#include "Temporal.h"
#include "util/ManagedTask.h"

#include <driver/gpio.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <atomic>
#include <cstdint>

using namespace OpenShock;

const uint32_t k_estopUpdateRate = 5;          // 200 Hz (debounce/hold timings live in EStopStateMachine)

const uint32_t k_estopTaskStackSize   = 4096;  // TODO: profile and tune
const UBaseType_t k_estopTaskPriority = 5;

static OpenShock::SimpleMutex s_estopMutex = {};
// Guarded via Mutex
static ManagedTask s_estopTask(TAG, "EStop task");
static gpio_num_t s_estopPin = GPIO_NUM_NC;  // Captured by value when the task starts

// Wrapped in atomics as they're read (or set via public methods) by Tasks potentially running on other cores.
static std::atomic<int64_t> s_estopActivatedAt = 0;  // When == 0, EStop not active. When != 0, EStop is active.
static std::atomic<bool> s_externallyTriggered = false;

static bool s_estopInitialized = false;

static void estopmgr_publishState(EStopState state, EStopState& lastState, TickType_t timeout = pdMS_TO_TICKS(750))
{
  if (state == lastState) {
    return;  // No state change -> no event
  }

  // Post the current state as the event payload
  esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_ESTOP_STATE_CHANGED, &state, sizeof(state), timeout);

  if (err == ESP_OK) {
    lastState = state;
  } else {
    OS_LOGE(TAG, "Failed to publish EStop event: %s", esp_err_to_name(err));
  }
}

static void estopmgr_latchWithoutTask();

// Samples the estop at a fixed rate and updates internal state + events
static void estopmgr_managerTask(gpio_num_t estopPin)
{
  EStopStateMachine machine;
  EStopState lastPublishedState = EStopState::Idle;

  // Carry over an E-Stop latched while no task was running (software trigger); it must be cleared with the button.
  int64_t latchedAt = s_estopActivatedAt.load(std::memory_order_relaxed);
  if (latchedAt != 0) {
    machine.Trigger(latchedAt);
    lastPublishedState = EStopState::Active;  // Already published when it was latched
  }

  // Check if killing manager was requested, continue looping otherwise
  while (!s_estopTask.stopRequested()) {
    // Sleep for the update rate
    vTaskDelay(pdMS_TO_TICKS(k_estopUpdateRate));

    // Get current time
    int64_t now = OpenShock::millis();

    // Handle external trigger: forcibly set the E-Stop active, otherwise sample the EStop input.
    if (s_externallyTriggered.exchange(false, std::memory_order_relaxed)) {
      machine.Trigger(now);
    } else {
      machine.Sample(gpio_get_level(estopPin), now);
    }

    // This task is the only writer while it runs; mirror before publishing so consumers see the new state.
    s_estopActivatedAt.store(machine.activatedAt(), std::memory_order_relaxed);

    estopmgr_publishState(machine.state(), lastPublishedState);
  }

  // Broke out of main loop, set global variables to Idle state.
  // Use a blocking publish so downstream consumers (keep-alive gating, visuals)
  // are guaranteed to observe the Idle transition even under event-loop pressure.
  estopmgr_publishState(EStopState::Idle, lastPublishedState, portMAX_DELAY);
  s_estopActivatedAt.store(0, std::memory_order_relaxed);
}

// Validates and configures `pin` as an EStop input. Does not touch s_estopPin
// or any other pin; caller owns the s_estopPin assignment and old-pin release.
static bool estopmgr_configurePin(gpio_num_t pin)
{
  if (!OpenShock::IsValidInputPin(pin)) {
    OS_LOGE(TAG, "Invalid EStop pin: %hhi", static_cast<int8_t>(pin));
    return false;
  }

  // Configure the new pin
  gpio_config_t io_conf = {
    .pin_bit_mask = 1ULL << pin,
    .mode         = GPIO_MODE_INPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type    = GPIO_INTR_DISABLE,
  };

  esp_err_t err = gpio_config(&io_conf);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to configure EStop pin: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

static void estopmgr_releasePin(gpio_num_t pin)
{
  if (pin == GPIO_NUM_NC) {
    return;
  }

  esp_err_t err = gpio_reset_pin(pin);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to reset old EStop pin: %s", esp_err_to_name(err));
  }
}

static bool estopmgr_taskStart()
{
  if (s_estopTask.running()) {
    OS_LOGW(TAG, "Tried to enable EStop manager, but was already running");
    return true;
  }

  if (s_estopPin == GPIO_NUM_NC) {
    gpio_num_t pin = GPIO_NUM_NC;
    if (!OpenShock::Config::GetEStopGpioPin(pin)) {
      OS_LOGE(TAG, "Failed to get EStop pin from config");
      return false;
    }

    if (pin == GPIO_NUM_NC) {
      OS_LOGW(TAG, "No valid pin is defined, refusing to start task");
      return false;
    }

    if (!estopmgr_configurePin(pin)) {
      return false;
    }

    s_estopPin = pin;
  }

  // The pin is captured by value, so the task keeps sampling the pin it was started with even if s_estopPin changes.
  gpio_num_t pin = s_estopPin;
  if (!s_estopTask.start(TAG, k_estopTaskStackSize, k_estopTaskPriority, 1, [pin] { estopmgr_managerTask(pin); })) {
    OS_LOGE(TAG, "Failed to create EStop event handler task");
    return false;
  }

  return true;
}

static bool estopmgr_taskStop()
{
  if (!s_estopTask.running()) {
    OS_LOGW(TAG, "Tried to kill EStop manager, but was not running");
    return true;
  }

  s_estopTask.stop();

  // Disable E-Stop after task has stopped to ensure that the task didn't get it stuck in enabled state
  s_estopActivatedAt.store(0, std::memory_order_relaxed);

  // A software trigger that arrived while the task was shutting down was never consumed; don't lose it.
  if (s_externallyTriggered.exchange(false, std::memory_order_relaxed)) {
    estopmgr_latchWithoutTask();
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
  if (!OpenShock::Config::GetEStopConfig(cfg)) {
    OS_LOGE(TAG, "Failed to get EStop pin from config");
    return false;
  }

  if (!cfg.enabled) {
    return true;
  }

  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (!estopmgr_configurePin(cfg.gpioPin)) {
    return false;
  }

  s_estopPin = cfg.gpioPin;

  return estopmgr_taskStart();
}

bool EStopManager::SetEStopEnabled(bool enabled)
{
  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (enabled) {
    if (!estopmgr_taskStart()) {
      return false;
    }
  } else {
    // Stopping the task resets the activation, which would release an active EStop without the hold-to-clear.
    if (EStopManager::IsEStopped()) {
      OS_LOGW(TAG, "Refusing to disable EStop while it is active");
      return false;
    }

    if (!estopmgr_taskStop()) {
      return false;
    }
  }

  if (!Config::SetEStopEnabled(enabled)) {
    OS_LOGE(TAG, "Failed to save EStop enabled state to config");
    return false;
  }

  return true;
}

bool EStopManager::SetEStopPin(gpio_num_t pin)
{
  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (s_estopPin == pin) {
    return true;
  }

  // Restarting the task resets the activation, which would release an active EStop without the hold-to-clear.
  if (EStopManager::IsEStopped()) {
    OS_LOGW(TAG, "Refusing to change EStop pin while EStop is active");
    return false;
  }

  // Configure the new pin before touching anything else. If this fails the
  // running task keeps sampling the old pin and the manager stays healthy.
  if (!estopmgr_configurePin(pin)) {
    return false;
  }

  gpio_num_t oldPin = s_estopPin;
  bool wasRunning   = s_estopTask.running();

  // Stop the task before swapping s_estopPin so the global can't disagree
  // with what the task is actually reading.
  if (wasRunning && !estopmgr_taskStop()) {
    // Roll back the configuration we just did; old pin and task are untouched.
    estopmgr_releasePin(pin);
    return false;
  }

  s_estopPin = pin;

  if (wasRunning && !estopmgr_taskStart()) {
    // Task is gone and we can't bring it back. The old pin is no longer being
    // sampled so we release it, but the manager is left inactive.
    estopmgr_releasePin(oldPin);
    return false;
  }

  estopmgr_releasePin(oldPin);

  if (!Config::SetEStopGpioPin(pin)) {
    OS_LOGE(TAG, "Failed to save EStop pin to config");
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

// Requires s_estopMutex. Activates the E-Stop directly when no manager task is running to pick up the trigger.
// Without a task there is no button to clear it, so it stays active until reboot or until a task (with a pin)
// takes it over and the button is held to clear it.
static void estopmgr_latchWithoutTask()
{
  int64_t expected = 0;
  if (!s_estopActivatedAt.compare_exchange_strong(expected, OpenShock::millis(), std::memory_order_relaxed)) {
    return;  // Already active
  }

  EStopState lastState = EStopState::Idle;
  estopmgr_publishState(EStopState::Active, lastState, portMAX_DELAY);
}

void EStopManager::SoftwareTrigger()
{
  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (s_estopTask.running()) {
    // Picked up by the manager task on its next tick
    s_externallyTriggered.store(true, std::memory_order_relaxed);
    return;
  }

  OS_LOGW(TAG, "Software EStop triggered without an EStop input configured, latching until reboot");
  estopmgr_latchWithoutTask();
}
