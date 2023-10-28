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
import { WiFiStateStore } from '$lib/stores';
import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import { WifiNetwork as FbsWifiNetwork } from '$lib/_fbs/open-shock/serialization/local/wifi-network';
import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
import { toastDelegator } from '$lib/stores/ToastDelegator';
import { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import { PairStateChangedEvent } from '$lib/_fbs/open-shock/serialization/local/pair-state-changed-event';
import { RfTxPinChangedEvent } from '$lib/_fbs/open-shock/serialization/local/rf-tx-pin-changed-event';

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

  WiFiStateStore.setScanStatus(payload.status());

  if (payload.status() === WifiScanStatus.Started) {
    toastDelegator.trigger({
      message: 'WiFi scan started',
      background: 'bg-green-500',
    });
  } else if (payload.status() === WifiScanStatus.Completed) {
    toastDelegator.trigger({
      message: 'WiFi scan completed',
      background: 'bg-green-500',
    });
  }
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

  WiFiStateStore.addNetwork(network);
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

  WiFiStateStore.addNetwork(network);
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

  WiFiStateStore.removeNetwork(bssid);
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

PayloadHandlers[DeviceToLocalMessagePayload.PairStateChangedEvent] = (cli, msg) => {
  const payload = new PairStateChangedEvent();
  msg.payload(payload);

  toastDelegator.trigger({
    message: 'Pairing state changed: ' + payload.status(),
    background: 'bg-green-500',
  });
};

PayloadHandlers[DeviceToLocalMessagePayload.RfTxPinChangedEvent] = (cli, msg) => {
  const payload = new RfTxPinChangedEvent();
  msg.payload(payload);

  toastDelegator.trigger({
    message: 'RF TX pin changed: ' + payload.pin(),
    background: 'bg-green-500',
  });
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
