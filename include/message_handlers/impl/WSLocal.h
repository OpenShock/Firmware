#pragma once

#include "serialization/_fbs/HubToLocalMessage_generated.h"
#include "serialization/_fbs/LocalToHubMessage_generated.h"

#include <cstdint>

#define HANDLER_SIG(NAME) void NAME(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* msg)
#define HANDLER_FN(NAME)  HANDLER_SIG(Handle##NAME)

namespace OpenShock::MessageHandlers::Local::_Private {
  typedef HANDLER_SIG((*HandlerType));
  HANDLER_FN(Common_ShockerCommandList);
  HANDLER_FN(InvalidMessage);
}  // namespace OpenShock::MessageHandlers::Local::_Private

#undef HANDLER_FN
#undef HANDLER_SIG
