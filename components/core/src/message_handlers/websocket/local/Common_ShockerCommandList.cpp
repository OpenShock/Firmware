#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "message_handlers/ShockerCommandList.h"

void OpenShock::MessageHandlers::Local::_Private::HandleCommon_ShockerCommandList(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* msg)
{
  (void)socketId;

  auto cmdList = msg->payload_as_Common_ShockerCommandList();
  if (cmdList == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as ShockerCommandList");
    return;
  }

  HandleShockerCommandList(cmdList);
}
