#include "CommandHandler.h"

#include "Board.h"
#include "config/Config.h"
#include "Constants.h"
#include "Logging.h"
#include "radio/RFTransmitter.h"
#include "Time.h"
#include "util/TaskUtils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <memory>
#include <unordered_map>

const char* const TAG = "CommandHandler";

const std::int64_t KEEP_ALIVE_INTERVAL  = 60'000;
const std::uint16_t KEEP_ALIVE_DURATION = 300;

using namespace OpenShock;

template<typename T>
constexpr T saturate(T value, T min, T max) {
  return std::min(std::max(value, min), max);
}
std::uint32_t calculateEepyTime(std::int64_t timeToKeepAlive) {
  std::int64_t now = OpenShock::millis();
  return static_cast<std::uint32_t>(saturate<std::int64_t>(timeToKeepAlive - now, 0LL, KEEP_ALIVE_INTERVAL));
}

struct KnownShocker {
  ShockerModelType model;
  std::uint16_t shockerId;
  std::int64_t lastActivityTimestamp;
  bool killTask;
};

static SemaphoreHandle_t s_rfTransmitterSemaphore     = nullptr;
static std::unique_ptr<RFTransmitter> s_rfTransmitter = nullptr;

static SemaphoreHandle_t s_keepAliveSemaphore = nullptr;
static QueueHandle_t s_keepAliveQueue         = nullptr;
static TaskHandle_t s_keepAliveTaskHandle     = nullptr;

void _keepAliveTask(void* arg) {
  std::int64_t timeToKeepAlive = KEEP_ALIVE_INTERVAL;

  // Map of shocker IDs to time of next keep-alive
  std::unordered_map<std::uint16_t, KnownShocker> activityMap;

  while (true) {
    // Calculate eepyTime based on the timeToKeepAlive
    std::uint32_t eepyTime = calculateEepyTime(timeToKeepAlive);

    KnownShocker cmd;
    while (xQueueReceive(s_keepAliveQueue, &cmd, pdMS_TO_TICKS(eepyTime)) == pdTRUE) {
      if (cmd.killTask) {
        ESP_LOGW(TAG, "Received kill command, exiting keep-alive task");
        vTaskDelete(nullptr);
        break;  // This should never be reached
      }

      activityMap[cmd.shockerId] = cmd;

      eepyTime = calculateEepyTime(std::min(timeToKeepAlive, cmd.lastActivityTimestamp + KEEP_ALIVE_INTERVAL));
    }

    // Update the time to now
    std::int64_t now = OpenShock::millis();

    // Keep track of the minimum activity time, so we know when to wake up
    timeToKeepAlive = now + KEEP_ALIVE_INTERVAL;

    // For every entry that has a keep-alive time less than now, send a keep-alive
    for (auto it = activityMap.begin(); it != activityMap.end(); ++it) {
      auto& cmd = it->second;

      if (cmd.lastActivityTimestamp + KEEP_ALIVE_INTERVAL < now) {
        ESP_LOGV(TAG, "Sending keep-alive for shocker %u", cmd.shockerId);

        if (s_rfTransmitter == nullptr) {
          ESP_LOGW(TAG, "RF Transmitter is not initialized, ignoring keep-alive");
          break;
        }

        if (!s_rfTransmitter->SendCommand(cmd.model, cmd.shockerId, ShockerCommandType::Vibrate, 0, KEEP_ALIVE_DURATION, false)) {
          ESP_LOGW(TAG, "Failed to send keep-alive for shocker %u", cmd.shockerId);
        }

        cmd.lastActivityTimestamp = now;
      }

      timeToKeepAlive = std::min(timeToKeepAlive, cmd.lastActivityTimestamp + KEEP_ALIVE_INTERVAL);
    }
  }
}

bool _internalSetKeepAliveEnabled(bool enabled) {
  bool wasEnabled = s_keepAliveQueue != nullptr && s_keepAliveTaskHandle != nullptr;

  if (enabled == wasEnabled) {
    ESP_LOGV(TAG, "keep-alive task is already %s", enabled ? "enabled" : "disabled");
    return true;
  }

  xSemaphoreTake(s_keepAliveSemaphore, portMAX_DELAY);

  if (enabled) {
    ESP_LOGV(TAG, "Enabling keep-alive task");

    s_keepAliveQueue = xQueueCreate(32, sizeof(KnownShocker));
    if (s_keepAliveQueue == nullptr) {
      ESP_LOGE(TAG, "Failed to create keep-alive task");

      xSemaphoreGive(s_keepAliveSemaphore);
      return false;
    }

    if (TaskUtils::TaskCreateExpensive(_keepAliveTask, "KeepAliveTask", 4096, nullptr, 1, &s_keepAliveTaskHandle) != pdPASS) {
      ESP_LOGE(TAG, "Failed to create keep-alive task");

      vQueueDelete(s_keepAliveQueue);
      s_keepAliveQueue = nullptr;

      xSemaphoreGive(s_keepAliveSemaphore);
      return false;
    }
  } else {
    ESP_LOGV(TAG, "Disabling keep-alive task");
    if (s_keepAliveTaskHandle != nullptr && s_keepAliveQueue != nullptr) {
      // Wait for the task to stop
      KnownShocker cmd {.killTask = true};
      while (eTaskGetState(s_keepAliveTaskHandle) != eDeleted) {
        vTaskDelay(pdMS_TO_TICKS(10));

        // Send nullptr to stop the task gracefully
        xQueueSend(s_keepAliveQueue, &cmd, pdMS_TO_TICKS(10));
      }
      vQueueDelete(s_keepAliveQueue);
      s_keepAliveQueue = nullptr;
    } else {
      ESP_LOGW(TAG, "keep-alive task is already disabled? Something might be wrong.");
    }
  }

  xSemaphoreGive(s_keepAliveSemaphore);

  return true;
}

bool CommandHandler::Init() {
  if (s_rfTransmitterSemaphore != nullptr) {
    ESP_LOGW(TAG, "RF Transmitter is already initialized");
    return true;
  }

  // Initialize semaphores
  s_rfTransmitterSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(s_rfTransmitterSemaphore);
  s_keepAliveSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(s_keepAliveSemaphore);

  auto& rfConfig = Config::GetRFConfig();

  std::uint8_t txPin = rfConfig.txPin;
  if (!OpenShock::IsValidOutputPin(txPin)) {
    if (!OpenShock::IsValidOutputPin(Constants::GPIO_RADIO_TX)) {
      ESP_LOGE(TAG, "Configured RF TX pin is invalid, default pin is also invalid. Pausing RF transmitter");
      Config::SetRFConfigTxPin(Constants::GPIO_INVALID);
      return false;
    }

    ESP_LOGW(TAG, "Configured RF TX pin is invalid, using default pin (%u)", Constants::GPIO_RADIO_TX);
    txPin = Constants::GPIO_RADIO_TX;
    Config::SetRFConfigTxPin(txPin);
  }

  s_rfTransmitter = std::make_unique<RFTransmitter>(txPin);
  if (!s_rfTransmitter->ok()) {
    ESP_LOGE(TAG, "Failed to initialize RF Transmitter");
    s_rfTransmitter = nullptr;
    return false;
  }

  if (rfConfig.keepAliveEnabled) {
    _internalSetKeepAliveEnabled(true);
  }

  return true;
}

bool CommandHandler::Ok() {
  return s_rfTransmitter != nullptr;
}

SetRfPinResultCode CommandHandler::SetRfTxPin(std::uint8_t txPin) {
  if (!OpenShock::IsValidOutputPin(txPin)) {
    return SetRfPinResultCode::InvalidPin;
  }

  xSemaphoreTake(s_rfTransmitterSemaphore, portMAX_DELAY);

  if (s_rfTransmitter != nullptr) {
    ESP_LOGV(TAG, "Destroying existing RF transmitter");
    s_rfTransmitter = nullptr;
  }

  ESP_LOGV(TAG, "Creating new RF transmitter");
  auto rfxmit = std::make_unique<RFTransmitter>(txPin);
  if (!rfxmit->ok()) {
    ESP_LOGE(TAG, "Failed to initialize RF transmitter");

    xSemaphoreGive(s_rfTransmitterSemaphore);
    return SetRfPinResultCode::InternalError;
  }

  if (!Config::SetRFConfigTxPin(txPin)) {
    ESP_LOGE(TAG, "Failed to set RF TX pin in config");

    xSemaphoreGive(s_rfTransmitterSemaphore);
    return SetRfPinResultCode::InternalError;
  }

  s_rfTransmitter = std::move(rfxmit);

  xSemaphoreGive(s_rfTransmitterSemaphore);
  return SetRfPinResultCode::Success;
}

bool CommandHandler::SetKeepAliveEnabled(bool enabled) {
  if (!_internalSetKeepAliveEnabled(enabled)) {
    return false;
  }

  if (!Config::SetRFConfigKeepAliveEnabled(enabled)) {
    ESP_LOGE(TAG, "Failed to set keep-alive enabled in config");
    return false;
  }

  return true;
}

bool CommandHandler::SetKeepAlivePaused(bool paused) {
  if (Config::GetRFConfig().keepAliveEnabled == false && paused == false) {
    ESP_LOGW(TAG, "Keep-alive is disabled in config, ignoring unpause command");
    return false;
  }
  if (!_internalSetKeepAliveEnabled(!paused)) {
    return false;
  }

  return true;
}

std::uint8_t CommandHandler::GetRfTxPin() {
  return Config::GetRFConfig().txPin;
}

bool CommandHandler::HandleCommand(ShockerModelType model, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, std::uint16_t durationMs) {
  xSemaphoreTake(s_rfTransmitterSemaphore, portMAX_DELAY);

  if (s_rfTransmitter == nullptr) {
    ESP_LOGW(TAG, "RF Transmitter is not initialized, ignoring command");

    xSemaphoreGive(s_rfTransmitterSemaphore);
    return false;
  }

  // Stop logic
  if (type == ShockerCommandType::Stop) {
    ESP_LOGV(TAG, "Stop command received, clearing pending commands");

    type       = ShockerCommandType::Vibrate;
    intensity  = 0;
    durationMs = 300;

    s_rfTransmitter->ClearPendingCommands();
  } else {
    ESP_LOGV(TAG, "Command received: %u %u %u %u", model, shockerId, type, intensity);
  }

  bool ok = s_rfTransmitter->SendCommand(model, shockerId, type, intensity, durationMs);

  xSemaphoreGive(s_rfTransmitterSemaphore);
  xSemaphoreTake(s_keepAliveSemaphore, portMAX_DELAY);

  if (ok && s_keepAliveQueue != nullptr) {
    KnownShocker cmd {.model = model, .shockerId = shockerId, .lastActivityTimestamp = OpenShock::millis() + durationMs};
    if (xQueueSend(s_keepAliveQueue, &cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
      ESP_LOGE(TAG, "Failed to send keep-alive command to queue");
    }
  }

  xSemaphoreGive(s_keepAliveSemaphore);

  return ok;
}
