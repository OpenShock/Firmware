#pragma once

#include "serialization/_fbs/HubToLocalMessage_generated.h"
#include "serialization/_fbs/LocalToHubMessage_generated.h"

#include <cstdint>

#define HANDLER_SIG(NAME) void NAME(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* msg)
#define HANDLER_FN(NAME)  HANDLER_SIG(Handle##NAME)

namespace OpenShock::MessageHandlers::Local::_Private {
  typedef HANDLER_SIG((*HandlerType));
  HANDLER_FN(WifiScanCommand);
  HANDLER_FN(WifiNetworkSaveCommand);
  HANDLER_FN(WifiNetworkForgetCommand);
  HANDLER_FN(WifiNetworkConnectCommand);
  HANDLER_FN(WifiNetworkDisconnectCommand);
  HANDLER_FN(OtaUpdateSetIsEnabledCommand);
  HANDLER_FN(OtaUpdateSetDomainCommand);
  HANDLER_FN(OtaUpdateSetUpdateChannelCommand);
  HANDLER_FN(OtaUpdateSetCheckIntervalCommand);
  HANDLER_FN(OtaUpdateSetAllowBackendManagementCommand);
  HANDLER_FN(OtaUpdateSetRequireManualApprovalCommand);
  HANDLER_FN(OtaUpdateHandleUpdateRequestCommand);
  HANDLER_FN(OtaUpdateCheckForUpdatesCommand);
  HANDLER_FN(OtaUpdateStartUpdateCommand);
  HANDLER_FN(AccountLinkCommand);
  HANDLER_FN(AccountUnlinkCommand);
  HANDLER_FN(SetRfTxPinCommand);
  HANDLER_FN(SetEstopEnabledCommand);
  HANDLER_FN(SetEstopPinCommand);
  HANDLER_FN(InvalidMessage);
}  // namespace OpenShock::MessageHandlers::Local::_Private

#undef HANDLER_FN
#undef HANDLER_SIG
