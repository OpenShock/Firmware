#include "event_handlers/impl/WSLocal.h"

#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Constants.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

void serializeSetRfTxPinResult(std::uint8_t socketId, std::uint8_t pin, OpenShock::Serialization::Local::SetRfPinResultCode result) {
  flatbuffers::FlatBufferBuilder builder(1024);

  auto responseOffset = builder.CreateStruct(OpenShock::Serialization::Local::SetRfTxPinCommandResult(pin, result));

  auto msgOffset = OpenShock::Serialization::Local::CreateDeviceToLocalMessage(builder, OpenShock::Serialization::Local::DeviceToLocalMessagePayload::SetRfTxPinCommandResult, responseOffset.Union());

  builder.Finish(msgOffset);

  auto buffer = builder.GetBufferPointer();
  auto size   = builder.GetSize();

  OpenShock::CaptivePortal::SendMessageBIN(socketId, buffer, size);
}

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleSetRfTxPinCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_SetRfTxPinCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as SetRfTxPinCommand");
    return;
  }

  auto pin = msg->pin();

  auto result = OpenShock::CommandHandler::SetRfTxPin(pin);

  serializeSetRfTxPinResult(socketId, pin, result);
}
