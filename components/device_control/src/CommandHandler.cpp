#include <freertos/FreeRTOS.h>

#include "CommandHandler.h"

const char* const TAG = "CommandHandler";

#include "Chipset.h"
#include "config/Config.h"
#include "estop/EStopManager.h"
#include "estop/EStopState.h"
#include "events/Events.h"
#include "Logging.h"

#include "OpenShock.h"
#include "radio/RFTransmitter.h"
#include "SimpleMutex.h"
#include "Temporal.h"
#include "util/ManagedTask.h"
#include <cstring>

#include <freertos/queue.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

const int64_t KEEP_ALIVE_INTERVAL  = 60'000;
const uint16_t KEEP_ALIVE_DURATION = 300;

static uint32_t calculateEepyTime(int64_t timeToKeepAlive)
{
  int64_t now = OpenShock::millis();
  return static_cast<uint32_t>(std::clamp<int64_t>(timeToKeepAlive - now, 0, KEEP_ALIVE_INTERVAL));
}

struct KnownShocker {
  bool killTask;
  OpenShock::ShockerModelType model;
  uint16_t shockerId;
  int64_t lastActivityTimestamp;
};

static OpenShock::SimpleMutex s_rfTransmitterMutex               = {};
static std::shared_ptr<OpenShock::RFTransmitter> s_rfTransmitter = nullptr;

static std::shared_ptr<OpenShock::RFTransmitter> GetTransmitter()
{
  OpenShock::ScopedLock lock__(&s_rfTransmitterMutex);
  return s_rfTransmitter;
}
static bool TryCreateTransmitter(gpio_num_t txPin)
{
  auto transmitter = std::make_shared<OpenShock::RFTransmitter>(txPin);
  if (!transmitter->ok()) {
    return false;
  }

  OpenShock::ScopedLock lock__(&s_rfTransmitterMutex);
  s_rfTransmitter = std::move(transmitter);
  return true;
}
static void DestroyTransmitter()
{
  OpenShock::ScopedLock lock__(&s_rfTransmitterMutex);
  s_rfTransmitter = nullptr;
}

static OpenShock::SimpleMutex s_keepAliveMutex = {};
static QueueHandle_t s_keepAliveQueue          = nullptr;
static OpenShock::ManagedTask s_keepAliveTask(TAG, "Keep-alive task");

using namespace OpenShock;

static void commandhandler_keepalivetask()
{
  int64_t timeToKeepAlive = KEEP_ALIVE_INTERVAL;

  // Map of (model, shocker ID) to its last activity; IDs are only unique per model
  std::unordered_map<uint32_t, KnownShocker> activityMap;

  while (true) {
    // Calculate eepyTime based on the timeToKeepAlive
    uint32_t eepyTime = calculateEepyTime(timeToKeepAlive);

    KnownShocker cmd;
    while (xQueueReceive(s_keepAliveQueue, &cmd, pdMS_TO_TICKS(eepyTime)) == pdTRUE) {
      if (cmd.killTask) {
        OS_LOGI(TAG, "Received kill command, exiting keep-alive task");
        return;
      }

      activityMap[(static_cast<uint32_t>(cmd.model) << 16) | cmd.shockerId] = cmd;

      eepyTime = calculateEepyTime(std::min(timeToKeepAlive, cmd.lastActivityTimestamp + KEEP_ALIVE_INTERVAL));
    }

    // Update the time to now
    int64_t now = OpenShock::millis();

    // Keep track of the minimum activity time, so we know when to wake up
    timeToKeepAlive = now + KEEP_ALIVE_INTERVAL;

    // For every entry that has a keep-alive time less than now, send a keep-alive
    for (auto it = activityMap.begin(); it != activityMap.end(); ++it) {
      auto& cmdRef = it->second;

      if (cmdRef.lastActivityTimestamp + KEEP_ALIVE_INTERVAL < now) {
        OS_LOGV(TAG, "Sending keep-alive for shocker %u", cmdRef.shockerId);

        auto transmitter = GetTransmitter();
        if (transmitter == nullptr) {
          OS_LOGW(TAG, "RF Transmitter is not initialized, ignoring keep-alive");
          break;
        }

        if (!transmitter->SendCommand(cmdRef.model, cmdRef.shockerId, ShockerCommandType::Vibrate, 0, KEEP_ALIVE_DURATION, false)) {
          OS_LOGW(TAG, "Failed to send keep-alive for shocker %u", cmdRef.shockerId);
        }

        cmdRef.lastActivityTimestamp = now;
      }

      timeToKeepAlive = std::min(timeToKeepAlive, cmdRef.lastActivityTimestamp + KEEP_ALIVE_INTERVAL);
    }
  }
}

static bool internalSetKeepAliveEnabled(bool enabled)
{
  // Called from the event loop (EStop changes) and from serial/portal tasks; the check must be under the lock.
  ScopedLock lock__(&s_keepAliveMutex);

  bool wasEnabled = s_keepAliveQueue != nullptr && s_keepAliveTask.running();

  if (enabled == wasEnabled) {
    return true;
  }

  if (enabled) {
    OS_LOGV(TAG, "Enabling keep-alive task");

    s_keepAliveQueue = xQueueCreate(32, sizeof(KnownShocker));
    if (s_keepAliveQueue == nullptr) {
      OS_LOGE(TAG, "Failed to create keep-alive task");
      return false;
    }

    if (!s_keepAliveTask.startExpensive("KeepAliveTask", 4096, 1, commandhandler_keepalivetask)) {  // PROFILED: 1.5KB stack usage
      OS_LOGE(TAG, "Failed to create keep-alive task");

      vQueueDelete(s_keepAliveQueue);
      s_keepAliveQueue = nullptr;

      return false;
    }
  } else {
    OS_LOGV(TAG, "Disabling keep-alive task");
    if (s_keepAliveTask.running() && s_keepAliveQueue != nullptr) {
      // The task blocks on its queue, so wake it with a kill command. Drop pending activity first so it always fits.
      s_keepAliveTask.stop([] {
        xQueueReset(s_keepAliveQueue);

        KnownShocker cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.killTask = true;
        if (xQueueSend(s_keepAliveQueue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
          OS_LOGE(TAG, "Failed to queue keep-alive kill command");
        }
      });
      vQueueDelete(s_keepAliveQueue);
      s_keepAliveQueue = nullptr;
    } else {
      OS_LOGW(TAG, "keep-alive task is already disabled? Something might be wrong.");
    }
  }

  return true;
}

static void commandhandler_handleestopstatechange(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  (void)event_handler_arg;
  (void)event_base;
  (void)event_id;

  EStopState state = *static_cast<EStopState*>(event_data);

  if (state != EStopState::Idle) {
    internalSetKeepAliveEnabled(false);
    return;
  }

  // E-Stop cleared: restore the configured keep-alive setting instead of forcing it on.
  bool keepAliveEnabled;
  if (!Config::GetRFConfigKeepAliveEnabled(keepAliveEnabled)) {
    OS_LOGE(TAG, "Failed to get keep-alive enabled from config");
    return;
  }

  internalSetKeepAliveEnabled(keepAliveEnabled);
}

bool CommandHandler::Init()
{
  esp_err_t err;

  static bool initialized = false;
  if (initialized) {
    OS_LOGW(TAG, "RF Transmitter and EStopManager are already initialized?");
    return true;
  }

  // Register first so EStop changes gate the keep-alive task even if the transmitter can't be created yet
  // (it can still be brought up later through SetRfTxPin).
  err = esp_event_handler_register(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_ESTOP_STATE_CHANGED, commandhandler_handleestopstatechange, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register event handler for OPENSHOCK_EVENTS: %s", esp_err_to_name(err));
    return false;
  }
  initialized = true;

  Config::RFConfig rfConfig;
  if (!Config::GetRFConfig(rfConfig)) {
    OS_LOGE(TAG, "Failed to get RF config");
    return false;
  }

  if (rfConfig.keepAliveEnabled && !EStopManager::IsEStopped()) {
    internalSetKeepAliveEnabled(true);
  }

  gpio_num_t txPin = rfConfig.txPin;
  if (!OpenShock::IsValidOutputPin(txPin)) {
    if (!OpenShock::IsValidOutputPin(OPENSHOCK_RF_TX_GPIO)) {
      OS_LOGE(TAG, "Configured RF TX pin (%hhi) is invalid, and default pin (%hhi) is invalid. Unable to initialize RF transmitter", txPin, OPENSHOCK_RF_TX_GPIO);

      OS_LOGD(TAG, "Setting RF TX pin to GPIO_INVALID");
      return Config::SetRFConfigTxPin(static_cast<gpio_num_t>(OPENSHOCK_GPIO_INVALID));  // This is not a error yet, unless we are unable to save the RF TX Pin as invalid
    }

    OS_LOGW(TAG, "Configured RF TX pin (%hhi) is invalid, using default pin (%hhi)", txPin, OPENSHOCK_RF_TX_GPIO);
    txPin = static_cast<gpio_num_t>(OPENSHOCK_RF_TX_GPIO);
    if (!Config::SetRFConfigTxPin(txPin)) {
      OS_LOGE(TAG, "Failed to set RF TX pin in config");
      return false;
    }
  }

  if (!TryCreateTransmitter(txPin)) {
    OS_LOGE(TAG, "Failed to initialize RF Transmitter");
    return false;
  }

  return true;
}

bool CommandHandler::Ok()
{
  return GetTransmitter() != nullptr;
}

SetGPIOResultCode CommandHandler::SetRfTxPin(gpio_num_t txPin)
{
  if (!OpenShock::IsValidOutputPin(txPin)) {
    return SetGPIOResultCode::InvalidPin;
  }

  // Driving the E-Stop input as the RF output would defeat the kill switch. Checked against the configured pin even
  // while the E-Stop is disabled, so enabling it later can't collide either.
  gpio_num_t estopPin = GPIO_NUM_NC;
  if (!Config::GetEStopGpioPin(estopPin)) {
    return SetGPIOResultCode::InternalError;
  }
  if (estopPin != GPIO_NUM_NC && txPin == estopPin) {
    return SetGPIOResultCode::PinInUse;
  }

  gpio_num_t oldPin = static_cast<gpio_num_t>(OPENSHOCK_GPIO_INVALID);
  if (auto current = GetTransmitter(); current != nullptr) {
    oldPin = current->GetTxPin();
  }

  if (oldPin != txPin) {
    // Free the old RMT channel first: two live channels can exhaust TX channels on smaller chips.
    DestroyTransmitter();

    OS_LOGV(TAG, "Creating new RF transmitter");
    if (!TryCreateTransmitter(txPin)) {
      OS_LOGE(TAG, "Failed to initialize RF transmitter");

      // Keep RF working on the previous pin rather than leaving it down
      if (OpenShock::IsValidOutputPin(oldPin) && !TryCreateTransmitter(oldPin)) {
        OS_LOGE(TAG, "Failed to restore RF transmitter on previous pin %hhi", oldPin);
      }

      return SetGPIOResultCode::InternalError;
    }
  }

  if (!Config::SetRFConfigTxPin(txPin)) {
    OS_LOGE(TAG, "Failed to set RF TX pin in config");
    return SetGPIOResultCode::InternalError;
  }

  return SetGPIOResultCode::Success;
}

bool CommandHandler::SetKeepAliveEnabled(bool enabled)
{
  // While e-stopped the task stays off; clearing the e-stop starts it from the saved setting.
  if (!internalSetKeepAliveEnabled(enabled && !EStopManager::IsEStopped())) {
    return false;
  }

  if (!Config::SetRFConfigKeepAliveEnabled(enabled)) {
    OS_LOGE(TAG, "Failed to set keep-alive enabled in config");
    return false;
  }

  return true;
}

gpio_num_t CommandHandler::GetRfTxPin()
{
  auto transmitter = GetTransmitter();
  if (transmitter != nullptr) {
    return transmitter->GetTxPin();
  }

  gpio_num_t txPin;
  if (!Config::GetRFConfigTxPin(txPin)) {
    OS_LOGE(TAG, "Failed to get RF TX pin from config");
    txPin = static_cast<gpio_num_t>(OPENSHOCK_GPIO_INVALID);
  }

  return txPin;
}

bool CommandHandler::HandleCommand(ShockerModelType model, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, uint16_t durationMs)
{
  if (EStopManager::IsEStopped()) {
    OS_LOGD(TAG, "Ignoring shocker command due to EmergencyStop being activated");
    return false;
  }

  auto transmitter = GetTransmitter();
  if (transmitter == nullptr) {
    OS_LOGW(TAG, "RF Transmitter is not initialized, ignoring command");
    return false;
  }

  bool ok = transmitter->SendCommand(model, shockerId, type, intensity, durationMs);

  ScopedLock lock__ka(&s_keepAliveMutex);

  if (ok && s_keepAliveQueue != nullptr) {
    KnownShocker cmd {.killTask = false, .model = model, .shockerId = shockerId, .lastActivityTimestamp = OpenShock::millis() + durationMs};
    if (xQueueSend(s_keepAliveQueue, &cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
      OS_LOGE(TAG, "Failed to send keep-alive command to queue");
    }
  }

  return ok;
}
