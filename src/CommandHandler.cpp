#include "CommandHandler.h"

#include "Config.h"
#include "Constants.h"
#include "RFTransmitter.h"
#include "Logging.h"

#include <memory>

const char* const TAG = "CommandHandler";

using namespace OpenShock;

static std::unique_ptr<RFTransmitter> s_rfTransmitter = nullptr;

bool CommandHandler::Init() {
  std::uint32_t txPin = Config::GetRFConfig().txPin;
  if (txPin == Constants::GPIO_INVALID) {
    ESP_LOGW(TAG, "Invalid RF TX pin loaded from config, ignoring");
    return false;
  }

  ESP_LOGD(TAG, "RF TX pin loaded from config: %u", txPin);

  s_rfTransmitter = std::make_unique<RFTransmitter>(txPin, 32);

  return true;
}

bool CommandHandler::Ok() {
  return s_rfTransmitter != nullptr && s_rfTransmitter->ok();
}

bool CommandHandler::SetRfTxPin(std::uint8_t txPin) {
  if (!Config::SetRFConfigTxPin(txPin)) {
    ESP_LOGW(TAG, "Failed to set RF TX pin");
    return false;
  }

  s_rfTransmitter = std::make_unique<RFTransmitter>(txPin, 32);

  return true;
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
    ESP_LOGV(TAG, "Command received: %u %u %u %u", (std::uint8_t)model, shockerId, (std::uint8_t)type, intensity);
  }

  return s_rfTransmitter->SendCommand(model, shockerId, type, intensity, durationMs);
}
