#include "CommandHandler.h"

#include "Config.h"
#include "RFTransmitter.h"

#include <esp_log.h>

#include <memory>

const char* const TAG = "CommandHandler";

using namespace OpenShock;

static std::unique_ptr<RFTransmitter> s_rfTransmitter = nullptr;

void CommandHandler::Init() {
  std::uint32_t txPin = Config::GetRFConfig().txPin;
  if (txPin > 60) {  // WARNING: This is a magic number, set a sensible constant for this
    ESP_LOGW(TAG, "TxPin is invalid, radio is disabled");
    return;
  }

  ESP_LOGD(TAG, "RMT/TX pin is: %d", txPin);

  s_rfTransmitter = std::make_unique<RFTransmitter>(txPin, 32);
}

bool CommandHandler::HandleCommand(std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, unsigned int duration, std::uint8_t shockerModel) {
  if (s_rfTransmitter == nullptr) return false;

  // Stop logic
  if (type == ShockerCommandType::Stop) {
    type      = ShockerCommandType::Vibrate;
    intensity = 0;
    duration  = 300;

    s_rfTransmitter->ClearPendingCommands();
  }

  return s_rfTransmitter->SendCommand(shockerModel, shockerId, type, intensity, duration);
}
