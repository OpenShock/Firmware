#include "event_handlers/impl/WSLocal.h"

#include "CaptivePortal.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

void serializeAccountLinkCommandResult(std::uint8_t socketId, OpenShock::Serialization::Local::AccountLinkResultCode result) {
  flatbuffers::FlatBufferBuilder builder(1024);

  auto responseOffset = builder.CreateStruct(OpenShock::Serialization::Local::AccountLinkCommandResult(result));

  auto msgOffset = OpenShock::Serialization::Local::CreateHubToLocalMessage(builder, OpenShock::Serialization::Local::HubToLocalMessagePayload::AccountLinkCommandResult, responseOffset.Union());

  builder.Finish(msgOffset);

  auto buffer = builder.GetBufferPointer();
  auto size   = builder.GetSize();

  OpenShock::CaptivePortal::SendMessageBIN(socketId, buffer, size);
}

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleAccountLinkCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root) {
  auto msg = root->payload_as_AccountLinkCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as AccountLinkCommand");
    return;
  }

  auto code = msg->code();

  if (code == nullptr) {
    serializeAccountLinkCommandResult(socketId, OpenShock::Serialization::Local::AccountLinkResultCode::CodeRequired);
    return;
  }

  if (code->size() != 6) {
    serializeAccountLinkCommandResult(socketId, OpenShock::Serialization::Local::AccountLinkResultCode::InvalidCodeLength);
    return;
  }

  auto result = GatewayConnectionManager::Link(*code);

  serializeAccountLinkCommandResult(socketId, result);
}
