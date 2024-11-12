#include "message_handlers/impl/WSLocal.h"

#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleOtaUpdateCheckForUpdatesCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root)
{
  auto msg = root->payload_as_OtaUpdateCheckForUpdatesCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as OtaUpdateCheckForUpdatesCommand");
    return;
  }

  // TODO
}
