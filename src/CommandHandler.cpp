#include "CommandHandler.h"

#include "RFTransmitter.h"
#include "Utils/FileUtils.h"

#include <esp_log.h>

#include <memory>

const char* const TAG = "CommandHandler";

using namespace OpenShock;

static std::unique_ptr<RFTransmitter> s_rfTransmitter = nullptr;

void CommandHandler::Init() {
#ifdef OPENSHOCK_TX_PIN
  int rmtPin;
  if (!FileUtils::TryReadFile("/rfPin", reinterpret_cast<std::uint8_t*>(&rmtPin), sizeof(rmtPin))) {
    rmtPin = OPENSHOCK_TX_PIN;
  }
  ESP_LOGD(TAG, "RMT/TX pin is: %d", rmtPin);

  s_rfTransmitter = std::make_unique<RFTransmitter>(rmtPin, 32);
#else
  ESP_LOGW(TAG, "No TX pin configured at build time! RADIO IS DISABLED");
#endif
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
