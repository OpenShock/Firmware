#include "CommandHandler.h"

#include "Board.h"
#include "Config.h"
#include "Constants.h"
#include "Logging.h"
#include "radio/RFTransmitter.h"
#include "util/TaskUtils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <memory>
#include <unordered_map>

const char* const TAG = "CommandHandler";

const std::uint16_t KEEP_ALIVE_INTERVAL = 60'000;

using namespace OpenShock;

struct ShockerCommand {
  ShockerModelType model;
  std::uint16_t shockerId;
  std::int64_t lastActivity;
};

static std::unique_ptr<RFTransmitter> s_rfTransmitter = nullptr;
static QueueHandle_t m_keepAliveQueue                 = nullptr;
static TaskHandle_t m_keepAliveTaskHandle             = nullptr;

void _keepAliveTask(void* arg) {
  std::int64_t nextKeepAlive = OpenShock::millis() + KEEP_ALIVE_INTERVAL;

  // Map of shocker IDs to time of next keep alive
  std::unordered_map<std::uint16_t, ShockerCommand> activityMap;

  while (true) {
    std::int64_t now = OpenShock::millis();
    if (now < nextKeepAlive) {
      std::uint32_t timeUntilNextKeepAlive = static_cast<std::uint32_t>(std::max(std::min(nextKeepAlive - now, std::int64_t(UINT32_MAX)), 0LL));

      ESP_LOGV(TAG, "Waiting for keep alive command for %ums", timeUntilNextKeepAlive);

      ShockerCommand cmd;
      if (xQueueReceive(m_keepAliveQueue, &cmd, pdMS_TO_TICKS(timeUntilNextKeepAlive)) == pdTRUE) {
        ESP_LOGV(TAG, "Command for shocker %u sent, adding/updating keep alive map, keep alive in %ums", cmd.shockerId, KEEP_ALIVE_INTERVAL);

        activityMap[cmd.shockerId] = cmd;

        nextKeepAlive = std::min(nextKeepAlive, cmd.lastActivity + KEEP_ALIVE_INTERVAL);

        continue;
      }

      now = OpenShock::millis();
    }

    // For every entry that has a keep alive time less than now, send a keep alive
    for (auto it = activityMap.begin(); it != activityMap.end(); ++it) {
      auto& cmd = it->second;

      if (cmd.lastActivity + KEEP_ALIVE_INTERVAL < now) {
        ESP_LOGV(TAG, "Sending keep alive for shocker %u", cmd.shockerId);

        if (s_rfTransmitter == nullptr) {
          ESP_LOGW(TAG, "RF Transmitter is not initialized, ignoring keep alive");
          break;
        }

        if (!s_rfTransmitter->SendCommand(cmd.model, cmd.shockerId, ShockerCommandType::Vibrate, 0, 300)) {
          ESP_LOGW(TAG, "Failed to send keep alive for shocker %u", cmd.shockerId);
        }

        cmd.lastActivity = now;

        nextKeepAlive = std::min(nextKeepAlive, cmd.lastActivity + KEEP_ALIVE_INTERVAL);
      }
    }
  }
}

bool CommandHandler::Init() {
  std::uint8_t txPin = Config::GetRFConfig().txPin;
  if (!OpenShock::IsValidOutputPin(txPin)) {
    ESP_LOGW(TAG, "Clearing invalid RF TX pin");
    Config::SetRFConfigTxPin(Constants::GPIO_INVALID);
    return false;
  }

  s_rfTransmitter = std::make_unique<RFTransmitter>(txPin, 32);
  if (!s_rfTransmitter->ok()) {
    ESP_LOGE(TAG, "Failed to initialize RF transmitter");
    s_rfTransmitter = nullptr;
    return false;
  }

  m_keepAliveQueue = xQueueCreate(32, sizeof(ShockerCommand));
  if (m_keepAliveQueue == nullptr) {
    ESP_LOGE(TAG, "Failed to create keep alive queue");
    return false;
  }

  if (TaskUtils::TaskCreateExpensive(_keepAliveTask, "KeepAliveTask", 4096, nullptr, 1, &m_keepAliveTaskHandle) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create keep alive task");
    return false;
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

  if (s_rfTransmitter != nullptr) {
    ESP_LOGV(TAG, "Destroying existing RF transmitter");
    s_rfTransmitter = nullptr;
  }

  ESP_LOGV(TAG, "Creating new RF transmitter");
  auto rfxmit = std::make_unique<RFTransmitter>(txPin, 32);
  if (!rfxmit->ok()) {
    ESP_LOGE(TAG, "Failed to initialize RF transmitter");
    return SetRfPinResultCode::InternalError;
  }

  if (!Config::SetRFConfigTxPin(txPin)) {
    ESP_LOGE(TAG, "Failed to set RF TX pin in config");
    return SetRfPinResultCode::InternalError;
  }

  s_rfTransmitter = std::move(rfxmit);

  return SetRfPinResultCode::Success;
}

std::uint8_t CommandHandler::GetRfTxPin() {
  return Config::GetRFConfig().txPin;
}

bool CommandHandler::HandleCommand(ShockerModelType model, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, std::uint16_t durationMs) {
  if (s_rfTransmitter == nullptr) {
    ESP_LOGW(TAG, "RF Transmitter is not initialized, ignoring command");
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

  ESP_LOGV(TAG, "Command sent: %u", ok);

  if (ok) {
    ESP_LOGV(TAG, "Command sent, adding keep alive for %u", shockerId);

    ShockerCommand cmd {.model = model, .shockerId = shockerId, .lastActivity = OpenShock::millis() + durationMs};
    if (xQueueSend(m_keepAliveQueue, &cmd, pdMS_TO_TICKS(10)) != pdTRUE) {
      ESP_LOGE(TAG, "Failed to send keep alive command to queue");
    }
  }

  return ok;
}
