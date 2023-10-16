#include "MessageHandlers/Local_Private.h"

#include "CommandHandler.h"
#include "Constants.h"

#include <esp_log.h>

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleSetRfTxPinCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_SetRfTxPinCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as SetRfTxPinCommand");
    return;
  }

  auto pin = msg->pin();

  if (pin == OpenShock::Constants::GPIO_INVALID) {
    ESP_LOGE(TAG, "Invalid pin specified");
    return;
  }

  CommandHandler::SetRfTxPin(pin);
}
