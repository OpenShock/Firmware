import { AccountLinkStatusEvent } from '#lib/_fbs/open-shock/serialization/local/account-link-status-event.js';
import { ErrorMessage } from '#lib/_fbs/open-shock/serialization/local/error-message.js';
import { HubToLocalMessagePayload } from '#lib/_fbs/open-shock/serialization/local/hub-to-local-message-payload.js';
import { HubToLocalMessage } from '#lib/_fbs/open-shock/serialization/local/hub-to-local-message.js';
import { LedSettingsChangedEvent } from '#lib/_fbs/open-shock/serialization/local/led-settings-changed-event.js';
import { LedStatusEvent } from '#lib/_fbs/open-shock/serialization/local/led-status-event.js';
import { ReadyMessage } from '#lib/_fbs/open-shock/serialization/local/ready-message.js';
import { WifiGotIpEvent } from '#lib/_fbs/open-shock/serialization/local/wifi-got-ip-event.js';
import { WifiScanStatusMessage } from '#lib/_fbs/open-shock/serialization/local/wifi-scan-status-message.js';
import { dispatchLedNotice } from '#lib/ledControl.js';
import { mapConfig, mapLedConfig } from '#lib/mappers/ConfigMapper.js';
import { mapLedNotice, mapLedStatus } from '#lib/mappers/LedStatusMapper.js';
import { hubState } from '#lib/stores/index.js';
import type { WebSocketClient } from '#lib/WebSocketClient.js';
import { ByteBuffer } from 'flatbuffers';
import { toast } from 'svelte-sonner';
import { mapWifiNetwork, WifiNetworkEventHandler } from './WifiNetworkEventHandler';

export type MessageHandler = (wsClient: WebSocketClient, message: HubToLocalMessage) => void;

function handleInvalidMessage() {
  console.warn('[WS] Received invalid message');
}

const PayloadTypes = Object.keys(HubToLocalMessagePayload).length / 2;
const PayloadHandlers: MessageHandler[] = new Array<MessageHandler>(PayloadTypes).fill(
  handleInvalidMessage
);

PayloadHandlers[HubToLocalMessagePayload.ReadyMessage] = (cli, msg) => {
  const payload = new ReadyMessage();
  msg.payload(payload);

  // A (re)connect starts from a clean slate: the hub re-sends its full network list after Ready, and anything
  // that changed while the socket was down would otherwise linger.
  hubState.clearWifiNetworks();
  hubState.wifiScanStatus = null;

  const connectedWifi = payload.connectedWifi();
  const connectedSSID = connectedWifi?.ssid();
  const connectedBSSID = connectedWifi?.bssid();
  hubState.setConnectedWifiNetwork(
    connectedWifi && connectedSSID && connectedBSSID
      ? mapWifiNetwork(connectedWifi, connectedSSID, connectedBSSID)
      : null
  );
  hubState.accountLinked = payload.accountLinked();
  hubState.config = mapConfig(payload);
  // A new connection holds no LED override: the hub ended the old one's when it went away
  hubState.clientId = payload.clientId();
  hubState.connectionEpoch++;
  const ledStatus = payload.ledStatus();
  hubState.ledStatus = ledStatus ? mapLedStatus(ledStatus) : null;

  const gpioValidInputs = payload.gpioValidInputsArray();
  if (gpioValidInputs) {
    hubState.gpioValidInputs = gpioValidInputs;
  }

  const gpioValidOutputs = payload.gpioValidOutputsArray();
  if (gpioValidOutputs) {
    hubState.gpioValidOutputs = gpioValidOutputs;
  }

  console.log('[WS] Updated hub state: ', hubState);

  toast.success('Websocket connection established');
};

PayloadHandlers[HubToLocalMessagePayload.ErrorMessage] = (cli, msg) => {
  const payload = new ErrorMessage();
  msg.payload(payload);

  console.error('[WS] Received error message: ', payload.message());

  toast.error(payload.message() ?? 'Unknown error');
};

PayloadHandlers[HubToLocalMessagePayload.WifiScanStatusMessage] = (cli, msg) => {
  const payload = new WifiScanStatusMessage();
  msg.payload(payload);

  hubState.wifiScanStatus = payload.status();
};

PayloadHandlers[HubToLocalMessagePayload.WifiNetworkEvent] = WifiNetworkEventHandler;

PayloadHandlers[HubToLocalMessagePayload.WifiGotIpEvent] = (cli, msg) => {
  const payload = new WifiGotIpEvent();
  msg.payload(payload);

  const ip = payload.ip();
  if (ip) {
    toast.info('Got IP address: ' + ip);
  }
};

PayloadHandlers[HubToLocalMessagePayload.AccountLinkStatusEvent] = (cli, msg) => {
  const payload = new AccountLinkStatusEvent();
  msg.payload(payload);

  hubState.accountLinked = payload.linked();
};

// A change from anywhere, this client included: the hub's settings as saved
PayloadHandlers[HubToLocalMessagePayload.LedSettingsChangedEvent] = (cli, msg) => {
  const payload = new LedSettingsChangedEvent();
  msg.payload(payload);

  const led = payload.led();
  if (led && hubState.config) hubState.config.led = mapLedConfig(led);
};

// What the LEDs show changed, an override ended, or an LED request failed; from any client
PayloadHandlers[HubToLocalMessagePayload.LedStatusEvent] = (cli, msg) => {
  const payload = new LedStatusEvent();
  msg.payload(payload);

  const status = payload.status();
  if (status) hubState.ledStatus = mapLedStatus(status);

  const notice = mapLedNotice(payload);
  if (notice) dispatchLedNotice(notice);
};

export function WebSocketMessageBinaryHandler(cli: WebSocketClient, data: ArrayBuffer) {
  const msg = HubToLocalMessage.getRootAsHubToLocalMessage(new ByteBuffer(new Uint8Array(data)));

  const payloadType = msg.payloadType();
  if (payloadType < 0 || payloadType >= PayloadHandlers.length) {
    console.error('[WS] ERROR: Received unknown payload type (out of bounds)');
    return;
  }

  try {
    PayloadHandlers[payloadType](cli, msg);
  } catch (e) {
    // A malformed message must not take the whole UI down (e.g. leave the config unset forever)
    console.error('[WS] ERROR: Failed to handle message: ', e);
    toast.error('Received an invalid message from the hub');
  }
}
