#include "event_handlers/impl/WSLocal.h"

#include "GatewayConnectionManager.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleAccountUnlinkCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  (void)socketId;
  
  auto msg = root->payload_as_AccountUnlinkCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as AccountUnlinkCommand");
    return;
  }

  GatewayConnectionManager::UnPair();
}
