#include "event_handlers/impl/WSGateway.h"

#include "Logging.h"


const char* const TAG = "ServerMessageHandlers";

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleInvalidMessage(const OpenShock::Serialization::Gateway::GatewayToDeviceMessage* root) {
  if (root == nullptr) {
    ESP_LOGE(TAG, "Message cannot be parsed");
    return;
  }

  ESP_LOGE(TAG, "Invalid message type: %u", root->payload_type());
}
