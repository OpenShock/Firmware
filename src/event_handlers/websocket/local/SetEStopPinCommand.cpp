#include "event_handlers/impl/WSLocal.h"

#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Common.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

void serializeSetEStopPinResult(std::uint8_t socketId, std::uint8_t pin, OpenShock::Serialization::Local::SetGPIOResultCode result) {
  flatbuffers::FlatBufferBuilder builder(1024);

  auto responseOffset = builder.CreateStruct(OpenShock::Serialization::Local::SetEstopPinCommandResult(pin, result));

  auto msgOffset = OpenShock::Serialization::Local::CreateHubToLocalMessage(builder, OpenShock::Serialization::Local::HubToLocalMessagePayload::SetEstopPinCommandResult, responseOffset.Union());

  builder.Finish(msgOffset);

  const std::uint8_t* buffer = builder.GetBufferPointer();
  std::uint8_t size          = builder.GetSize();

  OpenShock::CaptivePortal::SendMessageBIN(socketId, buffer, size);
}

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleSetEstopPinCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root) {
  auto msg = root->payload_as_SetEstopPinCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as SetEstopPinCommand");
    return;
  }

  auto pin = msg->pin();

  auto result = OpenShock::CommandHandler::SetEstopPin(pin);

  serializeSetEStopPinResult(socketId, pin, result);
}
