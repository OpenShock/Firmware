import { stopWifiScan } from '$lib/api';
import type { WebSocketClient } from '$lib/WebSocketClient';
import { ErrorMessage } from '$lib/_fbs/open-shock/serialization/local/error-message';
import { HubToLocalMessage } from '$lib/_fbs/open-shock/serialization/local/hub-to-local-message';
import { HubToLocalMessagePayload } from '$lib/_fbs/open-shock/serialization/local/hub-to-local-message-payload';
import { ReadyMessage } from '$lib/_fbs/open-shock/serialization/local/ready-message';
import { WifiScanStatusMessage } from '$lib/_fbs/open-shock/serialization/local/wifi-scan-status-message';
import { AccountLinkStatusEvent } from '$lib/_fbs/open-shock/serialization/local/account-link-status-event';
import { WifiGotIpEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-got-ip-event';
import { mapConfig } from '$lib/mappers/ConfigMapper';
import { hubState } from '$lib/stores';
import { ByteBuffer } from 'flatbuffers';
import { toast } from 'svelte-sonner';
import { WifiNetworkEventHandler } from './WifiNetworkEventHandler';

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

  hubState.wifiConnectedBSSID = payload.connectedWifi()?.bssid() || null;
  hubState.accountLinked = payload.accountLinked();
  hubState.config = mapConfig(payload.config());

  const gpioValidInputs = payload.gpioValidInputsArray();
  if (gpioValidInputs) {
    hubState.gpioValidInputs = gpioValidInputs;
  }

  const gpioValidOutputs = payload.gpioValidOutputsArray();
  if (gpioValidOutputs) {
    hubState.gpioValidOutputs = gpioValidOutputs;
  }

  console.log('[WS] Updated hub state: ', hubState);

  stopWifiScan();

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

export function WebSocketMessageBinaryHandler(cli: WebSocketClient, data: ArrayBuffer) {
  const msg = HubToLocalMessage.getRootAsHubToLocalMessage(new ByteBuffer(new Uint8Array(data)));

  const payloadType = msg.payloadType();
  if (payloadType < 0 || payloadType >= PayloadHandlers.length) {
    console.error('[WS] ERROR: Received unknown payload type (out of bounds)');
    return;
  }

  PayloadHandlers[payloadType](cli, msg);
}
