#pragma once

#include "serialization/_fbs/GatewayToHubMessage_generated.h"

#include <cstdint>

namespace OpenShock {
  class GatewayClient;
}

// `client` is the connection the message arrived on, which is not necessarily the manager's current one
#define HANDLER_SIG(NAME) void NAME(OpenShock::GatewayClient& client, const OpenShock::Serialization::Gateway::GatewayToHubMessage* msg)
#define HANDLER_FN(NAME)  HANDLER_SIG(Handle##NAME)

namespace OpenShock::MessageHandlers::Server::_Private {
  using HandlerType = HANDLER_SIG((*));
  HANDLER_FN(Ping);
  HANDLER_FN(Trigger);
  HANDLER_FN(ShockerCommandList);
  HANDLER_FN(OtaUpdateRequest);
  HANDLER_FN(InvalidMessage);
}  // namespace OpenShock::MessageHandlers::Server::_Private

#undef HANDLER_FN
#undef HANDLER_SIG
