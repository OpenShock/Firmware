import type { WebSocketClient } from '$lib/WebSocketClient';
import { WifiNetworkDiscoveredEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-network-discovered-event';
import { DeviceToLocalMessage } from '$lib/_fbs/open-shock/serialization/local/device-to-local-message';
import { DeviceToLocalMessagePayload } from '$lib/_fbs/open-shock/serialization/local/device-to-local-message-payload';
import { ReadyMessage } from '$lib/_fbs/open-shock/serialization/local/ready-message';
import { WifiScanStatusMessage } from '$lib/_fbs/open-shock/serialization/local/wifi-scan-status-message';
import { ByteBuffer } from 'flatbuffers';
import { WifiNetworkUpdatedEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-network-updated-event';
import { WifiNetworkLostEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-network-lost-event';
import { WifiNetworkSavedEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-network-saved-event';
import { WifiNetworkRemovedEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-network-removed-event';
import { WifiNetworkConnectedEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-network-connected-event';
import { WifiNetworkDisconnectedEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-network-disconnected-event';
import { DeviceStateStore } from '$lib/stores';
import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import { WifiNetwork as FbsWifiNetwork } from '$lib/_fbs/open-shock/serialization/local/wifi-network';
import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
import { toastDelegator } from '$lib/stores/ToastDelegator';
import { SetRfTxPinCommandResult } from '$lib/_fbs/open-shock/serialization/local/set-rf-tx-pin-command-result';
import { SetRfPinResultCode } from '$lib/_fbs/open-shock/serialization/local/set-rf-pin-result-code';
import { AccountLinkCommandResult } from '$lib/_fbs/open-shock/serialization/local/account-link-command-result';
import { AccountLinkResultCode } from '$lib/_fbs/open-shock/serialization/local/account-link-result-code';

type MessageHandler = (wsClient: WebSocketClient, message: DeviceToLocalMessage) => void;

function handleInvalidMessage() {
  console.warn('[WS] Received invalid message');
}

const PayloadTypes = Object.keys(DeviceToLocalMessagePayload).length / 2;
const PayloadHandlers: MessageHandler[] = new Array<MessageHandler>(PayloadTypes).fill(handleInvalidMessage);

PayloadHandlers[DeviceToLocalMessagePayload.ReadyMessage] = (cli, msg) => {
  const payload = new ReadyMessage();
  msg.payload(payload);

  console.log('[WS] Connected to device, poggies: ', payload.poggies());

  DeviceStateStore.update((store) => {
    store.wifiConnectedBSSID = payload.connectedWifi()?.bssid() || null;
    store.gatewayPaired = payload.gatewayPaired();
    store.rfTxPin = payload.rftxPin();
    return store;
  });

  const data = SerializeWifiScanCommand(true);
  cli.Send(data);

  toastDelegator.trigger({
    message: 'Websocket connection established',
    background: 'bg-green-500',
  });
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiScanStatusMessage] = (cli, msg) => {
  const payload = new WifiScanStatusMessage();
  msg.payload(payload);

  DeviceStateStore.setWifiScanStatus(payload.status());
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkDiscoveredEvent] = (cli, msg) => {
  const payload = new WifiNetworkDiscoveredEvent();
  msg.payload(payload);

  const fbsNetwork = new FbsWifiNetwork();
  payload.network(fbsNetwork);

  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network lost event');
    return;
  }

  const network: WiFiNetwork = {
    ssid: ssid,
    bssid: bssid,
    rssi: fbsNetwork.rssi(),
    channel: fbsNetwork.channel(),
    security: fbsNetwork.authMode(),
    saved: fbsNetwork.saved(),
  };

  DeviceStateStore.addWifiNetwork(network);
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkUpdatedEvent] = (cli, msg) => {
  const payload = new WifiNetworkUpdatedEvent();
  msg.payload(payload);

  const fbsNetwork = new FbsWifiNetwork();
  payload.network(fbsNetwork);

  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network lost event: ', fbsNetwork.ssid(), fbsNetwork.bssid(), fbsNetwork.rssi(), fbsNetwork.channel(), fbsNetwork.authMode(), fbsNetwork.saved());
    return;
  }

  const network: WiFiNetwork = {
    ssid: ssid,
    bssid: bssid,
    rssi: fbsNetwork.rssi(),
    channel: fbsNetwork.channel(),
    security: fbsNetwork.authMode(),
    saved: fbsNetwork.saved(),
  };

  DeviceStateStore.addWifiNetwork(network);
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkLostEvent] = (cli, msg) => {
  const payload = new WifiNetworkLostEvent();
  msg.payload(payload);

  const fbsNetwork = new FbsWifiNetwork();
  payload.network(fbsNetwork);

  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network lost event: ', fbsNetwork.ssid(), fbsNetwork.bssid(), fbsNetwork.rssi(), fbsNetwork.channel(), fbsNetwork.authMode(), fbsNetwork.saved());
    return;
  }

  DeviceStateStore.removeWifiNetwork(bssid);
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkSavedEvent] = (cli, msg) => {
  const payload = new WifiNetworkSavedEvent();
  msg.payload(payload);

  toastDelegator.trigger({
    message: 'WiFi network saved: ' + payload.network()?.ssid(),
    background: 'bg-green-500',
  });
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkRemovedEvent] = (cli, msg) => {
  const payload = new WifiNetworkRemovedEvent();
  msg.payload(payload);

  toastDelegator.trigger({
    message: 'WiFi network removed: ' + payload.network()?.ssid(),
    background: 'bg-green-500',
  });
};
PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkConnectedEvent] = (cli, msg) => {
  const payload = new WifiNetworkConnectedEvent();
  msg.payload(payload);

  toastDelegator.trigger({
    message: 'WiFi network connected: ' + payload.network()?.ssid(),
    background: 'bg-green-500',
  });
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkDisconnectedEvent] = (cli, msg) => {
  const payload = new WifiNetworkDisconnectedEvent();
  msg.payload(payload);

  toastDelegator.trigger({
    message: 'WiFi network disconnected: ' + payload.network()?.ssid(),
    background: 'bg-green-500',
  });
};

PayloadHandlers[DeviceToLocalMessagePayload.AccountLinkCommandResult] = (cli, msg) => {
  const payload = new AccountLinkCommandResult();
  msg.payload(payload);

  const result = payload.result();

  if (result == AccountLinkResultCode.Success) {
    toastDelegator.trigger({
      message: 'Gateway paired successfully',
      background: 'bg-green-500',
    });
  } else {
    let reason: string;
    switch (result) {
      case AccountLinkResultCode.CodeRequired:
        reason = 'Code required';
        break;
      case AccountLinkResultCode.InvalidCodeLength:
        reason = 'Invalid code length';
        break;
      case AccountLinkResultCode.NoInternetConnection:
        reason = 'No internet connection';
        break;
      case AccountLinkResultCode.InvalidCode:
        reason = 'Invalid code';
        break;
      case AccountLinkResultCode.InternalError:
        reason = 'Internal error';
        break;
      default:
        reason = 'Unknown';
        break;
    }
    toastDelegator.trigger({
      message: 'Failed to pair gateway: ' + reason,
      background: 'bg-red-500',
    });
  }
};

PayloadHandlers[DeviceToLocalMessagePayload.SetRfTxPinCommandResult] = (cli, msg) => {
  const payload = new SetRfTxPinCommandResult();
  msg.payload(payload);

  const result = payload.result();

  if (result == SetRfPinResultCode.Success) {
    DeviceStateStore.setRfTxPin(payload.pin());
    toastDelegator.trigger({
      message: 'Changed RF TX pin to: ' + payload.pin(),
      background: 'bg-green-500',
    });
  } else {
    let reason: string;
    switch (result) {
      case SetRfPinResultCode.InvalidPin:
        reason = 'Invalid pin';
        break;
      case SetRfPinResultCode.InternalError:
        reason = 'Internal error';
        break;
      default:
        reason = 'Unknown';
        break;
    }
    toastDelegator.trigger({
      message: 'Failed to change RF TX pin: ' + reason,
      background: 'bg-red-500',
    });
  }
};

export function WebSocketMessageBinaryHandler(cli: WebSocketClient, data: ArrayBuffer) {
  const msg = DeviceToLocalMessage.getRootAsDeviceToLocalMessage(new ByteBuffer(new Uint8Array(data)));

  const payloadType = msg.payloadType();
  if (payloadType >= PayloadHandlers.length) {
    console.error('[WS] ERROR: Received unknown payload type (out of bounds)');
    return;
  }

  PayloadHandlers[payloadType](cli, msg);
}
