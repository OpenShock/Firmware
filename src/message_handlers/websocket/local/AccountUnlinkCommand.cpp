#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "GatewayConnectionManager.h"
#include "Logging.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleAccountUnlinkCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root)
{
  (void)socketId;

  auto msg = root->payload_as_AccountUnlinkCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as AccountUnlinkCommand");
    return;
  }

  GatewayConnectionManager::UnLink();
}
