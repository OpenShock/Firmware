#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "CaptivePortal.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"

#include <cstdint>

void serializeAccountLinkCommandResult(uint8_t socketId, OpenShock::Serialization::Local::AccountLinkResultCode result)
{
  flatbuffers::FlatBufferBuilder builder(1024);  // TODO: Determine a good size

  auto responseOffset = OpenShock::Serialization::Local::CreateAccountLinkCommandResult(builder, result);

  auto msg = OpenShock::Serialization::Local::CreateHubToLocalMessage(builder, OpenShock::Serialization::Local::HubToLocalMessagePayload::AccountLinkCommandResult, responseOffset.Union());

  OpenShock::Serialization::Local::FinishHubToLocalMessageBuffer(builder, msg);

  auto buffer = builder.GetBufferPointer();
  auto size   = builder.GetSize();

  OpenShock::CaptivePortal::SendMessageBIN(socketId, buffer, size);
}

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleAccountLinkCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root)
{
  auto msg = root->payload_as_AccountLinkCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as AccountLinkCommand");
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
