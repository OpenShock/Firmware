import type { WebSocketClient } from '$lib/WebSocketClient';
import { WifiNetworkDiscoveredEvent } from '$lib/fbs/open-shock/serialization/local/wifi-network-discovered-event';
import { DeviceToLocalMessage } from '$lib/fbs/open-shock/serialization/local/device-to-local-message';
import { DeviceToLocalMessagePayload } from '$lib/fbs/open-shock/serialization/local/device-to-local-message-payload';
import { ReadyMessage } from '$lib/fbs/open-shock/serialization/local/ready-message';
import { WifiScanStatusMessage } from '$lib/fbs/open-shock/serialization/local/wifi-scan-status-message';
import { ByteBuffer } from 'flatbuffers';
import { WifiNetworkUpdatedEvent } from '$lib/fbs/open-shock/serialization/local/wifi-network-updated-event';
import { WifiNetworkLostEvent } from '$lib/fbs/open-shock/serialization/local/wifi-network-lost-event';
import { WifiNetworkSavedEvent } from '$lib/fbs/open-shock/serialization/local/wifi-network-saved-event';
import { WifiNetworkRemovedEvent } from '$lib/fbs/open-shock/serialization/local/wifi-network-removed-event';
import { WifiNetworkConnectedEvent } from '$lib/fbs/open-shock/serialization/local/wifi-network-connected-event';
import { WifiNetworkDisconnectedEvent } from '$lib/fbs/open-shock/serialization/local/wifi-network-disconnected-event';
import { WifiScanStatus } from '$lib/fbs/open-shock';
import { WiFiStateStore } from '$lib/stores';
import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import { WifiNetwork as FbsWifiNetwork } from '$lib/fbs/open-shock/serialization/local/wifi-network';
import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';

type MessageHandler = (wsClient: WebSocketClient, message: DeviceToLocalMessage) => void;

function handleInvalidMessage() {
  console.warn('[WS] Received invalid message');
}

const PayloadTypes = Object.keys(DeviceToLocalMessagePayload).length / 2;
const PayloadHandlers: MessageHandler[] = new Array<MessageHandler>(PayloadTypes).fill(handleInvalidMessage);

PayloadHandlers[DeviceToLocalMessagePayload.ReadyMessage] = (cli, msg) => {
  const payload = new ReadyMessage();
  msg.payload(payload);

  console.log('[WS] Received ready message, poggies: ', payload.poggies());

  const data = SerializeWifiScanCommand(true);
  cli.Send(data);
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiScanStatusMessage] = (cli, msg) => {
  const payload = new WifiScanStatusMessage();
  msg.payload(payload);

  switch (payload.status()) {
    case WifiScanStatus.Started:
      console.log('[WS] Received scan started message');
      WiFiStateStore.setScanning(true);
      break;
    case WifiScanStatus.InProgress:
      console.log('[WS] Received scan in progress message');
      WiFiStateStore.setScanning(true);
      break;
    case WifiScanStatus.Completed:
      console.log('[WS] Received scan completed message');
      WiFiStateStore.setScanning(false);
      break;
    case WifiScanStatus.Aborted:
      console.log('[WS] Received scan aborted message');
      WiFiStateStore.setScanning(false);
      break;
    case WifiScanStatus.Error:
      console.log('[WS] Received scan error message');
      WiFiStateStore.setScanning(false);
      break;
    default:
      console.warn('[WS] Received invalid scan status message:', payload.status());
      return;
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

  console.log('[WS] Received network discovered event: ', network);

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
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkRemovedEvent] = (cli, msg) => {
  const payload = new WifiNetworkRemovedEvent();
  msg.payload(payload);
};
PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkConnectedEvent] = (cli, msg) => {
  const payload = new WifiNetworkConnectedEvent();
  msg.payload(payload);
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkDisconnectedEvent] = (cli, msg) => {
  const payload = new WifiNetworkDisconnectedEvent();
  msg.payload(payload);
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
