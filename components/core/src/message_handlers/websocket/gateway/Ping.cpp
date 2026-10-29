#include <freertos/FreeRTOS.h>

#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "GatewayClient.h"
#include "Logging.h"
#include "serialization/WSGateway.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandlePing(OpenShock::GatewayClient& client, const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
{
  auto msg = root->payload_as_Ping();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as Ping");
    return;
  }

  // On the client the ping came in on: through the manager it could land on a successor that never got a ping
  client.markPingReceived();
  Serialization::Gateway::SerializePongMessage([&client](std::span<const uint8_t> data) { return client.sendMessageBIN(data); });
}
