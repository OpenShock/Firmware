#pragma once

#include "serialization/_fbs/ServerToDeviceMessage_generated.h"

#include <cstdint>

#define WS_EVENT_HANDLER_SIGNATURE(NAME) void NAME(const OpenShock::Serialization::ServerToDeviceMessage* msg)

namespace OpenShock::MessageHandlers::Server::_Private {
  typedef WS_EVENT_HANDLER_SIGNATURE((*HandlerType));
  WS_EVENT_HANDLER_SIGNATURE(HandleInvalidMessage);
  WS_EVENT_HANDLER_SIGNATURE(HandleShockerCommandList);
  WS_EVENT_HANDLER_SIGNATURE(HandleCaptivePortalConfig);
}  // namespace OpenShock::MessageHandlers::Server::_Private
