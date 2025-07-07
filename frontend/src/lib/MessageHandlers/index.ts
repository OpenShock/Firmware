import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
import type { WebSocketClient } from '$lib/WebSocketClient';
import { SetEstopEnabledCommand } from '$lib/_fbs/open-shock/serialization/local';
import { AccountLinkCommandResult } from '$lib/_fbs/open-shock/serialization/local/account-link-command-result';
import { AccountLinkResultCode } from '$lib/_fbs/open-shock/serialization/local/account-link-result-code';
import { ErrorMessage } from '$lib/_fbs/open-shock/serialization/local/error-message';
import { HubToLocalMessage } from '$lib/_fbs/open-shock/serialization/local/hub-to-local-message';
import { HubToLocalMessagePayload } from '$lib/_fbs/open-shock/serialization/local/hub-to-local-message-payload';
import { ReadyMessage } from '$lib/_fbs/open-shock/serialization/local/ready-message';
import { SetEstopEnabledCommandResult } from '$lib/_fbs/open-shock/serialization/local/set-estop-enabled-command-result';
import { SetEstopPinCommandResult } from '$lib/_fbs/open-shock/serialization/local/set-estop-pin-command-result';
import { SetGPIOResultCode } from '$lib/_fbs/open-shock/serialization/local/set-gpioresult-code';
import { SetRfTxPinCommandResult } from '$lib/_fbs/open-shock/serialization/local/set-rf-tx-pin-command-result';
import { WifiScanStatusMessage } from '$lib/_fbs/open-shock/serialization/local/wifi-scan-status-message';
import { mapConfig } from '$lib/mappers/ConfigMapper';
import { HubStateStore } from '$lib/stores';
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

  console.log('[WS] Connected to hub, poggies: ', payload.poggies());

  HubStateStore.update((store) => {
    store.wifiConnectedBSSID = payload.connectedWifi()?.bssid() || null;
    store.accountLinked = payload.accountLinked();
    store.config = mapConfig(payload.config());

    const gpioValidInputs = payload.gpioValidInputsArray();
    if (gpioValidInputs) {
      store.gpioValidInputs = gpioValidInputs;
    }

    const gpioValidOutputs = payload.gpioValidOutputsArray();
    if (gpioValidOutputs) {
      store.gpioValidOutputs = gpioValidOutputs;
    }

    console.log('[WS] Updated hub state store: ', store);

    return store;
  });

  const data = SerializeWifiScanCommand(true);
  cli.Send(data);

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

  HubStateStore.setWifiScanStatus(payload.status());
};

PayloadHandlers[HubToLocalMessagePayload.WifiNetworkEvent] = WifiNetworkEventHandler;

PayloadHandlers[HubToLocalMessagePayload.AccountLinkCommandResult] = (cli, msg) => {
  const payload = new AccountLinkCommandResult();
  msg.payload(payload);

  const result = payload.result();

  if (result == AccountLinkResultCode.Success) {
    toast.success('Account linked successfully');
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
      case AccountLinkResultCode.RateLimited:
        reason = 'Too many requests';
        break;
      case AccountLinkResultCode.InternalError:
        reason = 'Internal error';
        break;
      default:
        reason = 'Unknown';
        break;
    }
    toast.error('Failed to link account: ' + reason);
  }
};

PayloadHandlers[HubToLocalMessagePayload.SetRfTxPinCommandResult] = (cli, msg) => {
  const payload = new SetRfTxPinCommandResult();
  msg.payload(payload);

  const result = payload.result();

  if (result == SetGPIOResultCode.Success) {
    HubStateStore.setRfTxPin(payload.pin());
    toast.success('Changed RF TX pin to: ' + payload.pin());
  } else {
    let reason: string;
    switch (result) {
      case SetGPIOResultCode.InvalidPin:
        reason = 'Invalid pin';
        break;
      case SetGPIOResultCode.InternalError:
        reason = 'Internal error';
        break;
      default:
        reason = 'Unknown';
        break;
    }
    toast.error('Failed to change RF TX pin: ' + reason);
  }
};

PayloadHandlers[HubToLocalMessagePayload.SetEstopEnabledCommandResult] = (cli, msg) => {
  const payload = new SetEstopEnabledCommandResult();
  msg.payload(payload);

  const enabled = payload.enabled();
  const success = payload.success();

  if (success) {
    HubStateStore.setEstopEnabled(payload.enabled());
    toast.success('Changed EStop enabled to: ' + enabled);
  } else {
    toast.error('Failed to change EStop enabled');
  }
};

PayloadHandlers[HubToLocalMessagePayload.SetEstopPinCommandResult] = (cli, msg) => {
  const payload = new SetEstopPinCommandResult();
  msg.payload(payload);

  const result = payload.result();

  if (result == SetGPIOResultCode.Success) {
    const gpioPin = payload.gpioPin();
    HubStateStore.setEstopGpioPin(gpioPin);
    toast.success('Changed EStop pin to: ' + gpioPin);
  } else {
    let reason: string;
    switch (result) {
      case SetGPIOResultCode.InvalidPin:
        reason = 'Invalid pin';
        break;
      case SetGPIOResultCode.InternalError:
        reason = 'Internal error';
        break;
      default:
        reason = 'Unknown';
        break;
    }
    toast.error('Failed to change EStop pin: ' + reason);
  }
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
