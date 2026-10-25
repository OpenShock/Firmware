#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "Logging.h"

#include <utility>

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleInvalidMessage(OpenShock::GatewayClient&, const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
{
  if (root == nullptr) {
    OS_LOGE(TAG, "Message cannot be parsed");
    return;
  }

  OS_LOGE(TAG, "Invalid message type: %u", std::to_underlying(root->payload_type()));
}
