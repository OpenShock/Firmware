#include "event_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleInvalidMessage(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root) {
  (void)socketId;
  
  if (root == nullptr) {
    OS_LOGE(TAG, "Message cannot be parsed");
    return;
  }

  OS_LOGE(TAG, "Invalid message type: %d", root->payload_type());
}
