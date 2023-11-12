#pragma once

#include "serialization/_fbs/ServerToDeviceMessage_generated.h"

#include <cstdint>

#define _HANDLER_SIGNATURE(NAME) void NAME(const OpenShock::Serialization::ServerToDeviceMessage* msg)

namespace OpenShock::MessageHandlers::Server::_Private {
  typedef _HANDLER_SIGNATURE((*HandlerType));
  _HANDLER_SIGNATURE(HandleInvalidMessage);
  _HANDLER_SIGNATURE(HandleShockerCommandList);
  _HANDLER_SIGNATURE(HandleCaptivePortalConfig);
}  // namespace OpenShock::MessageHandlers::Server::_Private
