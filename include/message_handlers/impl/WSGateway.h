#pragma once

#include "serialization/_fbs/GatewayToHubMessage_generated.h"

#include <cstdint>

#define HANDLER_SIG(NAME) void NAME(const OpenShock::Serialization::Gateway::GatewayToHubMessage* msg)
#define HANDLER_FN(NAME)  HANDLER_SIG(Handle##NAME)

namespace OpenShock::MessageHandlers::Server::_Private {
  typedef HANDLER_SIG((*HandlerType));
  HANDLER_FN(Ping);
  HANDLER_FN(Trigger);
  HANDLER_FN(ShockerCommandList);
  HANDLER_FN(OtaUpdateRequest);
  HANDLER_FN(InvalidMessage);
}  // namespace OpenShock::MessageHandlers::Server::_Private

#undef HANDLER_FN
#undef HANDLER_SIG
