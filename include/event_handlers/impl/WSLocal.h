#pragma once

#include "serialization/_fbs/HubToLocalMessage_generated.h"
#include "serialization/_fbs/LocalToHubMessage_generated.h"

#include <cstdint>

#define WS_EVENT_HANDLER_SIGNATURE(NAME) void NAME(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* msg)

namespace OpenShock::MessageHandlers::Local::_Private {
  typedef WS_EVENT_HANDLER_SIGNATURE((*HandlerType));
  WS_EVENT_HANDLER_SIGNATURE(HandleInvalidMessage);
  WS_EVENT_HANDLER_SIGNATURE(HandleWiFiScanCommand);
  WS_EVENT_HANDLER_SIGNATURE(HandleWiFiNetworkSaveCommand);
  WS_EVENT_HANDLER_SIGNATURE(HandleWiFiNetworkForgetCommand);
  WS_EVENT_HANDLER_SIGNATURE(HandleWiFiNetworkConnectCommand);
  WS_EVENT_HANDLER_SIGNATURE(HandleWiFiNetworkDisconnectCommand);
  WS_EVENT_HANDLER_SIGNATURE(HandleAccountLinkCommand);
  WS_EVENT_HANDLER_SIGNATURE(HandleAccountUnlinkCommand);
  WS_EVENT_HANDLER_SIGNATURE(HandleSetRfTxPinCommand);
  WS_EVENT_HANDLER_SIGNATURE(HandleSetEstopPinCommand);
}  // namespace OpenShock::MessageHandlers::Local::_Private
