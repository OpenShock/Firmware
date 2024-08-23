// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

import { AccountLinkCommand } from '../../../open-shock/serialization/local/account-link-command';
import { AccountUnlinkCommand } from '../../../open-shock/serialization/local/account-unlink-command';
import { OtaUpdateCheckForUpdatesCommand } from '../../../open-shock/serialization/local/ota-update-check-for-updates-command';
import { OtaUpdateHandleUpdateRequestCommand } from '../../../open-shock/serialization/local/ota-update-handle-update-request-command';
import { OtaUpdateSetAllowBackendManagementCommand } from '../../../open-shock/serialization/local/ota-update-set-allow-backend-management-command';
import { OtaUpdateSetCheckIntervalCommand } from '../../../open-shock/serialization/local/ota-update-set-check-interval-command';
import { OtaUpdateSetDomainCommand } from '../../../open-shock/serialization/local/ota-update-set-domain-command';
import { OtaUpdateSetIsEnabledCommand } from '../../../open-shock/serialization/local/ota-update-set-is-enabled-command';
import { OtaUpdateSetRequireManualApprovalCommand } from '../../../open-shock/serialization/local/ota-update-set-require-manual-approval-command';
import { OtaUpdateSetUpdateChannelCommand } from '../../../open-shock/serialization/local/ota-update-set-update-channel-command';
import { OtaUpdateStartUpdateCommand } from '../../../open-shock/serialization/local/ota-update-start-update-command';
import { SetEstopPinCommand } from '../../../open-shock/serialization/local/set-estop-pin-command';
import { SetRfTxPinCommand } from '../../../open-shock/serialization/local/set-rf-tx-pin-command';
import { WifiNetworkConnectCommand } from '../../../open-shock/serialization/local/wifi-network-connect-command';
import { WifiNetworkDisconnectCommand } from '../../../open-shock/serialization/local/wifi-network-disconnect-command';
import { WifiNetworkForgetCommand } from '../../../open-shock/serialization/local/wifi-network-forget-command';
import { WifiNetworkSaveCommand } from '../../../open-shock/serialization/local/wifi-network-save-command';
import { WifiScanCommand } from '../../../open-shock/serialization/local/wifi-scan-command';


export enum LocalToHubMessagePayload {
  NONE = 0,
  WifiScanCommand = 1,
  WifiNetworkSaveCommand = 2,
  WifiNetworkForgetCommand = 3,
  WifiNetworkConnectCommand = 4,
  WifiNetworkDisconnectCommand = 5,
  OtaUpdateSetIsEnabledCommand = 6,
  OtaUpdateSetDomainCommand = 7,
  OtaUpdateSetUpdateChannelCommand = 8,
  OtaUpdateSetCheckIntervalCommand = 9,
  OtaUpdateSetAllowBackendManagementCommand = 10,
  OtaUpdateSetRequireManualApprovalCommand = 11,
  OtaUpdateHandleUpdateRequestCommand = 12,
  OtaUpdateCheckForUpdatesCommand = 13,
  OtaUpdateStartUpdateCommand = 14,
  AccountLinkCommand = 15,
  AccountUnlinkCommand = 16,
  SetRfTxPinCommand = 17,
  SetEstopPinCommand = 18
}

export function unionToLocalToHubMessagePayload(
  type: LocalToHubMessagePayload,
  accessor: (obj:AccountLinkCommand|AccountUnlinkCommand|OtaUpdateCheckForUpdatesCommand|OtaUpdateHandleUpdateRequestCommand|OtaUpdateSetAllowBackendManagementCommand|OtaUpdateSetCheckIntervalCommand|OtaUpdateSetDomainCommand|OtaUpdateSetIsEnabledCommand|OtaUpdateSetRequireManualApprovalCommand|OtaUpdateSetUpdateChannelCommand|OtaUpdateStartUpdateCommand|SetEstopPinCommand|SetRfTxPinCommand|WifiNetworkConnectCommand|WifiNetworkDisconnectCommand|WifiNetworkForgetCommand|WifiNetworkSaveCommand|WifiScanCommand) => AccountLinkCommand|AccountUnlinkCommand|OtaUpdateCheckForUpdatesCommand|OtaUpdateHandleUpdateRequestCommand|OtaUpdateSetAllowBackendManagementCommand|OtaUpdateSetCheckIntervalCommand|OtaUpdateSetDomainCommand|OtaUpdateSetIsEnabledCommand|OtaUpdateSetRequireManualApprovalCommand|OtaUpdateSetUpdateChannelCommand|OtaUpdateStartUpdateCommand|SetEstopPinCommand|SetRfTxPinCommand|WifiNetworkConnectCommand|WifiNetworkDisconnectCommand|WifiNetworkForgetCommand|WifiNetworkSaveCommand|WifiScanCommand|null
): AccountLinkCommand|AccountUnlinkCommand|OtaUpdateCheckForUpdatesCommand|OtaUpdateHandleUpdateRequestCommand|OtaUpdateSetAllowBackendManagementCommand|OtaUpdateSetCheckIntervalCommand|OtaUpdateSetDomainCommand|OtaUpdateSetIsEnabledCommand|OtaUpdateSetRequireManualApprovalCommand|OtaUpdateSetUpdateChannelCommand|OtaUpdateStartUpdateCommand|SetEstopPinCommand|SetRfTxPinCommand|WifiNetworkConnectCommand|WifiNetworkDisconnectCommand|WifiNetworkForgetCommand|WifiNetworkSaveCommand|WifiScanCommand|null {
  switch(LocalToHubMessagePayload[type]) {
    case 'NONE': return null; 
    case 'WifiScanCommand': return accessor(new WifiScanCommand())! as WifiScanCommand;
    case 'WifiNetworkSaveCommand': return accessor(new WifiNetworkSaveCommand())! as WifiNetworkSaveCommand;
    case 'WifiNetworkForgetCommand': return accessor(new WifiNetworkForgetCommand())! as WifiNetworkForgetCommand;
    case 'WifiNetworkConnectCommand': return accessor(new WifiNetworkConnectCommand())! as WifiNetworkConnectCommand;
    case 'WifiNetworkDisconnectCommand': return accessor(new WifiNetworkDisconnectCommand())! as WifiNetworkDisconnectCommand;
    case 'OtaUpdateSetIsEnabledCommand': return accessor(new OtaUpdateSetIsEnabledCommand())! as OtaUpdateSetIsEnabledCommand;
    case 'OtaUpdateSetDomainCommand': return accessor(new OtaUpdateSetDomainCommand())! as OtaUpdateSetDomainCommand;
    case 'OtaUpdateSetUpdateChannelCommand': return accessor(new OtaUpdateSetUpdateChannelCommand())! as OtaUpdateSetUpdateChannelCommand;
    case 'OtaUpdateSetCheckIntervalCommand': return accessor(new OtaUpdateSetCheckIntervalCommand())! as OtaUpdateSetCheckIntervalCommand;
    case 'OtaUpdateSetAllowBackendManagementCommand': return accessor(new OtaUpdateSetAllowBackendManagementCommand())! as OtaUpdateSetAllowBackendManagementCommand;
    case 'OtaUpdateSetRequireManualApprovalCommand': return accessor(new OtaUpdateSetRequireManualApprovalCommand())! as OtaUpdateSetRequireManualApprovalCommand;
    case 'OtaUpdateHandleUpdateRequestCommand': return accessor(new OtaUpdateHandleUpdateRequestCommand())! as OtaUpdateHandleUpdateRequestCommand;
    case 'OtaUpdateCheckForUpdatesCommand': return accessor(new OtaUpdateCheckForUpdatesCommand())! as OtaUpdateCheckForUpdatesCommand;
    case 'OtaUpdateStartUpdateCommand': return accessor(new OtaUpdateStartUpdateCommand())! as OtaUpdateStartUpdateCommand;
    case 'AccountLinkCommand': return accessor(new AccountLinkCommand())! as AccountLinkCommand;
    case 'AccountUnlinkCommand': return accessor(new AccountUnlinkCommand())! as AccountUnlinkCommand;
    case 'SetRfTxPinCommand': return accessor(new SetRfTxPinCommand())! as SetRfTxPinCommand;
    case 'SetEstopPinCommand': return accessor(new SetEstopPinCommand())! as SetEstopPinCommand;
    default: return null;
  }
}

export function unionListToLocalToHubMessagePayload(
  type: LocalToHubMessagePayload, 
  accessor: (index: number, obj:AccountLinkCommand|AccountUnlinkCommand|OtaUpdateCheckForUpdatesCommand|OtaUpdateHandleUpdateRequestCommand|OtaUpdateSetAllowBackendManagementCommand|OtaUpdateSetCheckIntervalCommand|OtaUpdateSetDomainCommand|OtaUpdateSetIsEnabledCommand|OtaUpdateSetRequireManualApprovalCommand|OtaUpdateSetUpdateChannelCommand|OtaUpdateStartUpdateCommand|SetEstopPinCommand|SetRfTxPinCommand|WifiNetworkConnectCommand|WifiNetworkDisconnectCommand|WifiNetworkForgetCommand|WifiNetworkSaveCommand|WifiScanCommand) => AccountLinkCommand|AccountUnlinkCommand|OtaUpdateCheckForUpdatesCommand|OtaUpdateHandleUpdateRequestCommand|OtaUpdateSetAllowBackendManagementCommand|OtaUpdateSetCheckIntervalCommand|OtaUpdateSetDomainCommand|OtaUpdateSetIsEnabledCommand|OtaUpdateSetRequireManualApprovalCommand|OtaUpdateSetUpdateChannelCommand|OtaUpdateStartUpdateCommand|SetEstopPinCommand|SetRfTxPinCommand|WifiNetworkConnectCommand|WifiNetworkDisconnectCommand|WifiNetworkForgetCommand|WifiNetworkSaveCommand|WifiScanCommand|null, 
  index: number
): AccountLinkCommand|AccountUnlinkCommand|OtaUpdateCheckForUpdatesCommand|OtaUpdateHandleUpdateRequestCommand|OtaUpdateSetAllowBackendManagementCommand|OtaUpdateSetCheckIntervalCommand|OtaUpdateSetDomainCommand|OtaUpdateSetIsEnabledCommand|OtaUpdateSetRequireManualApprovalCommand|OtaUpdateSetUpdateChannelCommand|OtaUpdateStartUpdateCommand|SetEstopPinCommand|SetRfTxPinCommand|WifiNetworkConnectCommand|WifiNetworkDisconnectCommand|WifiNetworkForgetCommand|WifiNetworkSaveCommand|WifiScanCommand|null {
  switch(LocalToHubMessagePayload[type]) {
    case 'NONE': return null; 
    case 'WifiScanCommand': return accessor(index, new WifiScanCommand())! as WifiScanCommand;
    case 'WifiNetworkSaveCommand': return accessor(index, new WifiNetworkSaveCommand())! as WifiNetworkSaveCommand;
    case 'WifiNetworkForgetCommand': return accessor(index, new WifiNetworkForgetCommand())! as WifiNetworkForgetCommand;
    case 'WifiNetworkConnectCommand': return accessor(index, new WifiNetworkConnectCommand())! as WifiNetworkConnectCommand;
    case 'WifiNetworkDisconnectCommand': return accessor(index, new WifiNetworkDisconnectCommand())! as WifiNetworkDisconnectCommand;
    case 'OtaUpdateSetIsEnabledCommand': return accessor(index, new OtaUpdateSetIsEnabledCommand())! as OtaUpdateSetIsEnabledCommand;
    case 'OtaUpdateSetDomainCommand': return accessor(index, new OtaUpdateSetDomainCommand())! as OtaUpdateSetDomainCommand;
    case 'OtaUpdateSetUpdateChannelCommand': return accessor(index, new OtaUpdateSetUpdateChannelCommand())! as OtaUpdateSetUpdateChannelCommand;
    case 'OtaUpdateSetCheckIntervalCommand': return accessor(index, new OtaUpdateSetCheckIntervalCommand())! as OtaUpdateSetCheckIntervalCommand;
    case 'OtaUpdateSetAllowBackendManagementCommand': return accessor(index, new OtaUpdateSetAllowBackendManagementCommand())! as OtaUpdateSetAllowBackendManagementCommand;
    case 'OtaUpdateSetRequireManualApprovalCommand': return accessor(index, new OtaUpdateSetRequireManualApprovalCommand())! as OtaUpdateSetRequireManualApprovalCommand;
    case 'OtaUpdateHandleUpdateRequestCommand': return accessor(index, new OtaUpdateHandleUpdateRequestCommand())! as OtaUpdateHandleUpdateRequestCommand;
    case 'OtaUpdateCheckForUpdatesCommand': return accessor(index, new OtaUpdateCheckForUpdatesCommand())! as OtaUpdateCheckForUpdatesCommand;
    case 'OtaUpdateStartUpdateCommand': return accessor(index, new OtaUpdateStartUpdateCommand())! as OtaUpdateStartUpdateCommand;
    case 'AccountLinkCommand': return accessor(index, new AccountLinkCommand())! as AccountLinkCommand;
    case 'AccountUnlinkCommand': return accessor(index, new AccountUnlinkCommand())! as AccountUnlinkCommand;
    case 'SetRfTxPinCommand': return accessor(index, new SetRfTxPinCommand())! as SetRfTxPinCommand;
    case 'SetEstopPinCommand': return accessor(index, new SetEstopPinCommand())! as SetEstopPinCommand;
    default: return null;
  }
}
