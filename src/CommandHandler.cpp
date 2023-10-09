#include "CommandHandler.h"

#include "Config.h"
#include "Constants.h"
#include "RFTransmitter.h"

#include <esp_log.h>

#include <memory>

const char* const TAG = "CommandHandler";

using namespace OpenShock;

static std::unique_ptr<RFTransmitter> s_rfTransmitter = nullptr;

void CommandHandler::Init() {
  std::uint32_t txPin = Config::GetRFConfig().txPin;
  if (txPin > OpenShock::Constants::MaxGpioPin) {
    ESP_LOGW(TAG, "TxPin is invalid, radio is disabled");
    return;
  }

  ESP_LOGD(TAG, "RMT/TX pin is: %d", txPin);

  s_rfTransmitter = std::make_unique<RFTransmitter>(txPin, 32);
}

bool CommandHandler::HandleCommand(ShockerModelType model, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, unsigned int duration) {
  if (s_rfTransmitter == nullptr) return false;

  // Stop logic
  if (type == ShockerCommandType::Stop) {
    ESP_LOGV(TAG, "Stop command received, clearing pending commands");

    type      = ShockerCommandType::Vibrate;
    intensity = 0;
    duration  = 300;

    s_rfTransmitter->ClearPendingCommands();
  } else {
    ESP_LOGV(TAG, "Command received: %u %u %u %u", model, shockerId, type, intensity);
  }

  return s_rfTransmitter->SendCommand(model, shockerId, type, intensity, duration);
}
