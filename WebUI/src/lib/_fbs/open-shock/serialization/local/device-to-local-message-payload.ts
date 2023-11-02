// automatically generated by the FlatBuffers compiler, do not modify

import { AccountLinkCommandResult } from '../../../open-shock/serialization/local/account-link-command-result.js';
import { ErrorMessage } from '../../../open-shock/serialization/local/error-message.js';
import { ReadyMessage } from '../../../open-shock/serialization/local/ready-message.js';
import { SetRfTxPinCommandResult } from '../../../open-shock/serialization/local/set-rf-tx-pin-command-result.js';
import { WifiNetworkConnectedEvent } from '../../../open-shock/serialization/local/wifi-network-connected-event.js';
import { WifiNetworkDisconnectedEvent } from '../../../open-shock/serialization/local/wifi-network-disconnected-event.js';
import { WifiNetworkDiscoveredEvent } from '../../../open-shock/serialization/local/wifi-network-discovered-event.js';
import { WifiNetworkLostEvent } from '../../../open-shock/serialization/local/wifi-network-lost-event.js';
import { WifiNetworkRemovedEvent } from '../../../open-shock/serialization/local/wifi-network-removed-event.js';
import { WifiNetworkSavedEvent } from '../../../open-shock/serialization/local/wifi-network-saved-event.js';
import { WifiNetworkUpdatedEvent } from '../../../open-shock/serialization/local/wifi-network-updated-event.js';
import { WifiScanStatusMessage } from '../../../open-shock/serialization/local/wifi-scan-status-message.js';


export enum DeviceToLocalMessagePayload {
  NONE = 0,
  ReadyMessage = 1,
  ErrorMessage = 2,
  WifiScanStatusMessage = 3,
  WifiNetworkDiscoveredEvent = 4,
  WifiNetworkUpdatedEvent = 5,
  WifiNetworkLostEvent = 6,
  WifiNetworkSavedEvent = 7,
  WifiNetworkRemovedEvent = 8,
  WifiNetworkConnectedEvent = 9,
  WifiNetworkDisconnectedEvent = 10,
  AccountLinkCommandResult = 11,
  SetRfTxPinCommandResult = 12
}

export function unionToDeviceToLocalMessagePayload(
  type: DeviceToLocalMessagePayload,
  accessor: (obj:AccountLinkCommandResult|ErrorMessage|ReadyMessage|SetRfTxPinCommandResult|WifiNetworkConnectedEvent|WifiNetworkDisconnectedEvent|WifiNetworkDiscoveredEvent|WifiNetworkLostEvent|WifiNetworkRemovedEvent|WifiNetworkSavedEvent|WifiNetworkUpdatedEvent|WifiScanStatusMessage) => AccountLinkCommandResult|ErrorMessage|ReadyMessage|SetRfTxPinCommandResult|WifiNetworkConnectedEvent|WifiNetworkDisconnectedEvent|WifiNetworkDiscoveredEvent|WifiNetworkLostEvent|WifiNetworkRemovedEvent|WifiNetworkSavedEvent|WifiNetworkUpdatedEvent|WifiScanStatusMessage|null
): AccountLinkCommandResult|ErrorMessage|ReadyMessage|SetRfTxPinCommandResult|WifiNetworkConnectedEvent|WifiNetworkDisconnectedEvent|WifiNetworkDiscoveredEvent|WifiNetworkLostEvent|WifiNetworkRemovedEvent|WifiNetworkSavedEvent|WifiNetworkUpdatedEvent|WifiScanStatusMessage|null {
  switch(DeviceToLocalMessagePayload[type]) {
    case 'NONE': return null; 
    case 'ReadyMessage': return accessor(new ReadyMessage())! as ReadyMessage;
    case 'ErrorMessage': return accessor(new ErrorMessage())! as ErrorMessage;
    case 'WifiScanStatusMessage': return accessor(new WifiScanStatusMessage())! as WifiScanStatusMessage;
    case 'WifiNetworkDiscoveredEvent': return accessor(new WifiNetworkDiscoveredEvent())! as WifiNetworkDiscoveredEvent;
    case 'WifiNetworkUpdatedEvent': return accessor(new WifiNetworkUpdatedEvent())! as WifiNetworkUpdatedEvent;
    case 'WifiNetworkLostEvent': return accessor(new WifiNetworkLostEvent())! as WifiNetworkLostEvent;
    case 'WifiNetworkSavedEvent': return accessor(new WifiNetworkSavedEvent())! as WifiNetworkSavedEvent;
    case 'WifiNetworkRemovedEvent': return accessor(new WifiNetworkRemovedEvent())! as WifiNetworkRemovedEvent;
    case 'WifiNetworkConnectedEvent': return accessor(new WifiNetworkConnectedEvent())! as WifiNetworkConnectedEvent;
    case 'WifiNetworkDisconnectedEvent': return accessor(new WifiNetworkDisconnectedEvent())! as WifiNetworkDisconnectedEvent;
    case 'AccountLinkCommandResult': return accessor(new AccountLinkCommandResult())! as AccountLinkCommandResult;
    case 'SetRfTxPinCommandResult': return accessor(new SetRfTxPinCommandResult())! as SetRfTxPinCommandResult;
    default: return null;
  }
}

export function unionListToDeviceToLocalMessagePayload(
  type: DeviceToLocalMessagePayload, 
  accessor: (index: number, obj:AccountLinkCommandResult|ErrorMessage|ReadyMessage|SetRfTxPinCommandResult|WifiNetworkConnectedEvent|WifiNetworkDisconnectedEvent|WifiNetworkDiscoveredEvent|WifiNetworkLostEvent|WifiNetworkRemovedEvent|WifiNetworkSavedEvent|WifiNetworkUpdatedEvent|WifiScanStatusMessage) => AccountLinkCommandResult|ErrorMessage|ReadyMessage|SetRfTxPinCommandResult|WifiNetworkConnectedEvent|WifiNetworkDisconnectedEvent|WifiNetworkDiscoveredEvent|WifiNetworkLostEvent|WifiNetworkRemovedEvent|WifiNetworkSavedEvent|WifiNetworkUpdatedEvent|WifiScanStatusMessage|null, 
  index: number
): AccountLinkCommandResult|ErrorMessage|ReadyMessage|SetRfTxPinCommandResult|WifiNetworkConnectedEvent|WifiNetworkDisconnectedEvent|WifiNetworkDiscoveredEvent|WifiNetworkLostEvent|WifiNetworkRemovedEvent|WifiNetworkSavedEvent|WifiNetworkUpdatedEvent|WifiScanStatusMessage|null {
  switch(DeviceToLocalMessagePayload[type]) {
    case 'NONE': return null; 
    case 'ReadyMessage': return accessor(index, new ReadyMessage())! as ReadyMessage;
    case 'ErrorMessage': return accessor(index, new ErrorMessage())! as ErrorMessage;
    case 'WifiScanStatusMessage': return accessor(index, new WifiScanStatusMessage())! as WifiScanStatusMessage;
    case 'WifiNetworkDiscoveredEvent': return accessor(index, new WifiNetworkDiscoveredEvent())! as WifiNetworkDiscoveredEvent;
    case 'WifiNetworkUpdatedEvent': return accessor(index, new WifiNetworkUpdatedEvent())! as WifiNetworkUpdatedEvent;
    case 'WifiNetworkLostEvent': return accessor(index, new WifiNetworkLostEvent())! as WifiNetworkLostEvent;
    case 'WifiNetworkSavedEvent': return accessor(index, new WifiNetworkSavedEvent())! as WifiNetworkSavedEvent;
    case 'WifiNetworkRemovedEvent': return accessor(index, new WifiNetworkRemovedEvent())! as WifiNetworkRemovedEvent;
    case 'WifiNetworkConnectedEvent': return accessor(index, new WifiNetworkConnectedEvent())! as WifiNetworkConnectedEvent;
    case 'WifiNetworkDisconnectedEvent': return accessor(index, new WifiNetworkDisconnectedEvent())! as WifiNetworkDisconnectedEvent;
    case 'AccountLinkCommandResult': return accessor(index, new AccountLinkCommandResult())! as AccountLinkCommandResult;
    case 'SetRfTxPinCommandResult': return accessor(index, new SetRfTxPinCommandResult())! as SetRfTxPinCommandResult;
    default: return null;
  }
}
