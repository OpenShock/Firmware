#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "Logging.h"

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleInvalidMessage(const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
{
  if (root == nullptr) {
    OS_LOGE(TAG, "Message cannot be parsed");
    return;
  }

  OS_LOGE(TAG, "Invalid message type: %hhu", static_cast<uint8_t>(root->payload_type()));
}
