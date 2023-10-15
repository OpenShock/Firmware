#include "MessageHandlers/Server_Private.h"

#include <esp_log.h>

const char* const TAG = "ServerMessageHandlers";

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleInvalidMessage(std::uint8_t socketId, const OpenShock::Serialization::ServerToDeviceMessage* root) {
  if (root == nullptr) {
    ESP_LOGE(TAG, "Message cannot be parsed");
    return;
  }

  ESP_LOGE(TAG, "Invalid message type: %d", root->payload_type());
}
