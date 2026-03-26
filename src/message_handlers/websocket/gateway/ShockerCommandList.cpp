#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "Logging.h"
#include "message_handlers/ShockerCommandList.h"

void OpenShock::MessageHandlers::Server::_Private::HandleShockerCommandList(const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
{
  auto cmdList = root->payload_as_Common_ShockerCommandList();
  if (cmdList == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as ShockerCommandList");
    return;
  }

  OpenShock::MessageHandlers::HandleShockerCommandList(cmdList);
}
