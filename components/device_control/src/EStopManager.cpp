#include <freertos/FreeRTOS.h>

#include "estop/EStopManager.h"

const char* const TAG = "EStopManager";

#include "Chipset.h"
#include "estop/EStopConfig.h"
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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <optional>

using namespace OpenShock;

static OpenShock::SimpleMutex s_estopMutex = {};
// Guarded via Mutex
static ManagedTask s_estopTask(TAG, "EStop task");
static gpio_num_t s_estopPin = GPIO_NUM_NC;  // Captured by value when the task starts

// Wrapped in atomics as they're read (or set via public methods) by Tasks potentially running on other cores.
static std::atomic<int64_t> s_estopActivatedAt = 0;  // When == 0, EStop not active. When != 0, EStop is active.
// Mirrors `s_estopActivatedAt != 0` for IsEStopped(), which runs on every command and every RF frame:
// std::atomic<int64_t> isn't lock-free on these chips (every access takes a global spinlock), a bool is.
// Set no later than the timestamp and cleared with it, so IsEStopped() never reports clear while it is set.
static std::atomic<bool> s_estopActive = false;
// A software trigger not yet taken over by the task. IsEStopped() reports it at once, so the trigger needs no lock
// and doesn't wait for the next tick.
static std::atomic<bool> s_externallyTriggered = false;

static bool s_estopInitialized = false;

// The E-Stop latch as last written to (or read from) the config store; nullopt when that is not known. Owned by Init
// until the task starts, then by the task: one task runs at a time, and start and stop are serialized by s_estopMutex.
static std::optional<bool> s_latchStored = std::nullopt;

// How long to wait before writing the latch again after a write failed (a busy store is retried on the next sample).
const int64_t k_latchRetryInterval = 1000;

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

// Stores the latch when it differs from what is stored. Returns whether it touched flash, which takes the task off its
// fixed sample rate for the length of the write.
static bool estopmgr_storeLatch(bool latched, int64_t now, int64_t& nextAttempt)
{
  if (s_latchStored == latched || now < nextAttempt) {
    return false;
  }

  switch (Config::SaveEStopLatched(latched)) {
    case Config::LatchSave::Saved:
      s_latchStored = latched;
      return true;
    case Config::LatchSave::Busy:
      return false;  // Another config writer: try again on the next sample
    case Config::LatchSave::Failed:
    default:
      OS_LOGE(TAG, "Failed to store the E-Stop latch (%s), retrying", latched ? "latched" : "released");
      nextAttempt = now + k_latchRetryInterval;
      return true;
  }
}

// Samples the estop at a fixed rate and updates internal state + events
static void estopmgr_managerTask(gpio_num_t estopPin)
{
  EStopStateMachine machine;
  EStopState lastPublishedState = EStopState::Idle;

  // Carry over an E-Stop latched while no task was running (software trigger, or the latch stored before a reboot);
  // it must be cleared with the button.
  int64_t latchedAt = s_estopActivatedAt.load(std::memory_order_relaxed);
  if (latchedAt != 0) {
    machine.Trigger(latchedAt);
    lastPublishedState = EStopState::Active;  // Already published when it was latched
  }

  // On the task watchdog for as long as it samples: a wedged sampler means the kill switch silently stops working.
  // Fed every 5 ms sample, so only a real hang (not a slow post or log) can trip it.
  TaskUtils::TaskWatchdog watchdog;

  int64_t nextLatchAttempt = 0;

  // Check if killing manager was requested, continue looping otherwise
  TickType_t lastWake = xTaskGetTickCount();
  while (!s_estopTask.stopRequested()) {
    watchdog.feed();
    // Fixed rate: the debounce and hold timings (EStopStateMachine) assume samples this far apart, whatever the loop
    // body costs
    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONFIG_OPENSHOCK_ESTOP_POLL_INTERVAL_MS));

    // Get current time
    int64_t now = OpenShock::millis();

    // Handle external trigger: forcibly set the E-Stop active, otherwise sample the EStop input.
    bool triggered = s_externallyTriggered.load();
    if (triggered) {
      machine.Trigger(now);
    } else {
      machine.Sample(gpio_get_level(estopPin), now);
    }

    // This task is the only writer while it runs; mirror before publishing so consumers see the new state.
    s_estopActivatedAt.store(machine.activatedAt());
    s_estopActive.store(machine.activatedAt() != 0);

    // Cleared only once the activation is mirrored, so IsEStopped() never sees neither
    if (triggered) {
      s_externallyTriggered.store(false);
    }

    estopmgr_publishState(machine.state(), lastPublishedState);

    // Only once the state is mirrored and published: the E-Stop is in effect before its latch reaches flash, and a
    // slow write never delays it.
    if (estopmgr_storeLatch(machine.activatedAt() != 0, now, nextLatchAttempt)) {
      // Resume the fixed rate from here instead of catching up with back-to-back samples, which would shorten the
      // debounce window.
      lastWake = xTaskGetTickCount();
    }
  }

  // An active E-Stop survives the stop: s_estopActivatedAt stays set, and the next task takes it over (or, with no
  // task, it stays latched until reboot). Clearing it here would release it without the hold-to-clear.
  if (machine.activatedAt() != 0) {
    return;
  }

  estopmgr_publishState(EStopState::Idle, lastPublishedState);
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
    auto configPin = OpenShock::Config::GetEStopGpioPin();
    if (!configPin) {
      OS_LOGE(TAG, "Failed to get EStop pin from config");
      return false;
    }

    gpio_num_t pin = *configPin;
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
  if (!s_estopTask.start(TAG, CONFIG_OPENSHOCK_ESTOP_TASK_STACK_SIZE, CONFIG_OPENSHOCK_ESTOP_TASK_PRIORITY, 1, [pin] { estopmgr_managerTask(pin); })) {
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

  // The task left s_estopActivatedAt set if it was active (see estopmgr_managerTask). A software trigger that arrived
  // while it was shutting down was never consumed; don't lose it.
  if (s_externallyTriggered.load()) {
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

  auto cfg = OpenShock::Config::GetEStopConfig();
  if (!cfg) {
    OS_LOGE(TAG, "Failed to get EStop pin from config");
    return false;
  }

  // Read before anything can transmit: CommandHandler and the RF transmitter start after this.
  bool latched  = OpenShock::Config::LoadEStopLatched();
  s_latchStored = latched;

  if (!cfg->enabled) {
    if (latched) {
      // Without an input there is no button to clear it, so it would stop the hub for good. Drop it.
      OS_LOGW(TAG, "E-Stop was latched, but the E-Stop is disabled; releasing the latch");
      if (OpenShock::Config::SaveEStopLatched(false) == OpenShock::Config::LatchSave::Saved) {
        s_latchStored = false;
      }
    }
    return true;
  }

  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (latched) {
    // Active from here on, through IsEStopped(); the task takes it over and it is cleared with the button. Even if the
    // input can't be set up below, it stays active until a reboot that can.
    OS_LOGW(TAG, "E-Stop was latched before the restart, keeping it active until it is cleared with the button");
    s_estopActivatedAt.store(std::max<int64_t>(OpenShock::millis(), 1));
    s_estopActive.store(true);
    EStopState lastState = EStopState::Idle;
    estopmgr_publishState(EStopState::Active, lastState);
  }

  if (!estopmgr_configurePin(cfg->gpioPin)) {
    return false;
  }

  s_estopPin = cfg->gpioPin;

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
    // Task is gone and we can't bring it back, so no pin is sampled. Fail safe: latch until reboot, which restarts
    // the task on the pin still in config.
    OS_LOGE(TAG, "Failed to restart EStop task, latching EStop until reboot");
    estopmgr_latchWithoutTask();
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
  return s_estopActive.load() || s_externallyTriggered.load();
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
  bool latched     = s_estopActivatedAt.compare_exchange_strong(expected, OpenShock::millis());
  s_estopActive.store(true);           // Active either way: just latched, or already was
  s_externallyTriggered.store(false);  // Taken over by the latch
  if (!latched) {
    return;                            // Already active
  }

  // Bounded: the latch itself is already in effect, the event only informs listeners
  EStopState lastState = EStopState::Idle;
  estopmgr_publishState(EStopState::Active, lastState);
}

void EStopManager::SoftwareTrigger()
{
  // In effect immediately through IsEStopped(), without waiting on s_estopMutex (held across task restarts and
  // config writes) or on the task's next tick.
  s_externallyTriggered.store(true);

  OpenShock::ScopedLock lock__(&s_estopMutex);

  if (s_estopTask.running()) {
    return;  // Picked up by the manager task on its next tick
  }

  OS_LOGW(TAG, "Software EStop triggered without an EStop input configured, latching until reboot");
  estopmgr_latchWithoutTask();
}
