#pragma once

#include "_fbs/LocalToDeviceMessage_generated.h"

#include <cstdint>

#define _HANDLER_SIGNATURE(NAME) void NAME(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* msg)

namespace OpenShock::MessageHandlers::Local::_Private {
  typedef _HANDLER_SIGNATURE((*HandlerType));
  _HANDLER_SIGNATURE(HandleInvalidMessage);
  _HANDLER_SIGNATURE(HandleWiFiScanCommand);
  _HANDLER_SIGNATURE(HandleWiFiNetworkSaveCommand);
  _HANDLER_SIGNATURE(HandleWiFiNetworkForgetCommand);
  _HANDLER_SIGNATURE(HandleWiFiNetworkConnectCommand);
  _HANDLER_SIGNATURE(HandleWiFiNetworkDisconnectCommand);
  _HANDLER_SIGNATURE(HandleGatewayPairCommand);
  _HANDLER_SIGNATURE(HandleGatewayUnpairCommand);
  _HANDLER_SIGNATURE(HandleSetRfTxPinCommand);
}  // namespace OpenShock::MessageHandlers::Local::_Private
