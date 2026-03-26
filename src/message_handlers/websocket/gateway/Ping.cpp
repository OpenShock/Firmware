#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "serialization/WSGateway.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandlePing(const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
{
  auto msg = root->payload_as_Ping();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as Ping");
    return;
  }

  Serialization::Gateway::SerializePongMessage(GatewayConnectionManager::SendMessageBIN);
}
