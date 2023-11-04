#include "CommandHandler.h"

#include "Board.h"
#include "Config.h"
#include "Constants.h"
#include "Logging.h"
#include "RFTransmitter.h"

#include <memory>

const char* const TAG = "CommandHandler";

using namespace OpenShock;

static std::unique_ptr<RFTransmitter> s_rfTransmitter = nullptr;

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

  return s_rfTransmitter->SendCommand(model, shockerId, type, intensity, durationMs);
}
