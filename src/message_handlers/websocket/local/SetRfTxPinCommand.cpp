#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "captiveportal/Manager.h"
#include "CommandHandler.h"
#include "Common.h"
#include "Logging.h"

#include <cstdint>

void serializeSetRfTxPinResult(uint8_t socketId, gpio_num_t pin, OpenShock::Serialization::Local::SetGPIOResultCode result)
{
  flatbuffers::FlatBufferBuilder builder(1024);

  auto responseOffset = OpenShock::Serialization::Local::CreateSetRfTxPinCommandResult(builder, static_cast<int8_t>(pin), result);

  auto msg = OpenShock::Serialization::Local::CreateHubToLocalMessage(builder, OpenShock::Serialization::Local::HubToLocalMessagePayload::SetRfTxPinCommandResult, responseOffset.Union());

  OpenShock::Serialization::Local::FinishHubToLocalMessageBuffer(builder, msg);

  OpenShock::CaptivePortal::SendMessageBIN(socketId, builder.GetBufferSpan());
}

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleSetRfTxPinCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root)
{
  auto msg = root->payload_as_SetRfTxPinCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as SetRfTxPinCommand");
    return;
  }

  auto pin = msg->pin();

  auto result = OpenShock::CommandHandler::SetRfTxPin(static_cast<gpio_num_t>(pin));

  serializeSetRfTxPinResult(socketId, static_cast<gpio_num_t>(pin), result);
}
