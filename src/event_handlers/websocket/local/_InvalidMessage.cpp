#include "event_handlers/impl/WSLocal.h"

#include "Logging.h"

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleInvalidMessage(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  if (root == nullptr) {
    ESP_LOGE(TAG, "Message cannot be parsed");
    return;
  }

  ESP_LOGE(TAG, "Invalid message type: %d", root->payload_type());
}
