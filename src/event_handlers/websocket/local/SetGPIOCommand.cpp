#include "event_handlers/impl/WSLocal.h"

#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Common.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

void serializeSetGPIOResult(std::uint8_t socketId, std::uint8_t pin, OpenShock::Serialization::Local::SetGPIOResultCode result, OpenShock::Serialization::Local::DeviceToLocalMessagePayload payloadType) {
  flatbuffers::FlatBufferBuilder builder(1024);

  auto responseOffset = builder.CreateStruct(OpenShock::Serialization::Local::SetRfTxPinCommandResult(pin, result));

  auto msgOffset = OpenShock::Serialization::Local::CreateDeviceToLocalMessage(builder, payloadType, responseOffset.Union());

  builder.Finish(msgOffset);

  const std::uint8_t* buffer = builder.GetBufferPointer();
  std::uint8_t size          = builder.GetSize();

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

  serializeSetGPIOResult(socketId, pin, result, OpenShock::Serialization::Local::DeviceToLocalMessagePayload::SetRfTxPinCommandResult);
}

void _Private::HandleSetEstopPinCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_SetEstopPinCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as SetEstopPinCommand");
    return;
  }

  auto pin = msg->pin();

  auto result = OpenShock::CommandHandler::SetEstopPin(pin);

  serializeSetGPIOResult(socketId, pin, result, OpenShock::Serialization::Local::DeviceToLocalMessagePayload::SetEstopPinCommandResult);
}
