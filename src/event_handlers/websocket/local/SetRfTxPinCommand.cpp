#include "event_handlers/impl/WSLocal.h"

#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Common.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

void serializeSetRfTxPinResult(uint8_t socketId, uint8_t pin, OpenShock::Serialization::Local::SetRfPinResultCode result) {
  flatbuffers::FlatBufferBuilder builder(1024);

  auto responseOffset = builder.CreateStruct(OpenShock::Serialization::Local::SetRfTxPinCommandResult(pin, result));

  auto msgOffset = OpenShock::Serialization::Local::CreateHubToLocalMessage(builder, OpenShock::Serialization::Local::HubToLocalMessagePayload::SetRfTxPinCommandResult, responseOffset.Union());

  builder.Finish(msgOffset);

  const uint8_t* buffer = builder.GetBufferPointer();
  uint8_t size   = builder.GetSize();

  OpenShock::CaptivePortal::SendMessageBIN(socketId, buffer, size);
}

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleSetRfTxPinCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root) {
  auto msg = root->payload_as_SetRfTxPinCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as SetRfTxPinCommand");
    return;
  }

  auto pin = msg->pin();

  auto result = OpenShock::CommandHandler::SetRfTxPin(pin);

  serializeSetRfTxPinResult(socketId, pin, result);
}
