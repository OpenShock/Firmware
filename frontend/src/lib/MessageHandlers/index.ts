import type { WebSocketClient } from '$lib/WebSocketClient';
import { DeviceToLocalMessage } from '$lib/_fbs/open-shock/serialization/local/device-to-local-message';
import { DeviceToLocalMessagePayload } from '$lib/_fbs/open-shock/serialization/local/device-to-local-message-payload';
import { ReadyMessage } from '$lib/_fbs/open-shock/serialization/local/ready-message';
import { WifiScanStatusMessage } from '$lib/_fbs/open-shock/serialization/local/wifi-scan-status-message';
import { ByteBuffer } from 'flatbuffers';
import { DeviceStateStore } from '$lib/stores';
import { SerializeWifiScanCommand } from '$lib/Serializers/WifiScanCommand';
import { toastDelegator } from '$lib/stores/ToastDelegator';
import { SetRfTxPinCommandResult } from '$lib/_fbs/open-shock/serialization/local/set-rf-tx-pin-command-result';
import { SetRfPinResultCode } from '$lib/_fbs/open-shock/serialization/local/set-rf-pin-result-code';
import { AccountLinkCommandResult } from '$lib/_fbs/open-shock/serialization/local/account-link-command-result';
import { AccountLinkResultCode } from '$lib/_fbs/open-shock/serialization/local/account-link-result-code';
import { ErrorMessage } from '$lib/_fbs/open-shock/serialization/local/error-message';
import { WifiNetworkEventHandler } from './WifiNetworkEventHandler';

export type MessageHandler = (wsClient: WebSocketClient, message: DeviceToLocalMessage) => void;

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
    store.accountLinked = payload.accountLinked();
    store.rfTxPin = payload.config()?.rf()?.txPin() ?? 255; // 255 = invalid
    return store;
  });

  const data = SerializeWifiScanCommand(true);
  cli.Send(data);

  toastDelegator.trigger({
    message: 'Websocket connection established',
    background: 'bg-green-500',
  });
};

PayloadHandlers[DeviceToLocalMessagePayload.ErrorMessage] = (cli, msg) => {
  const payload = new ErrorMessage();
  msg.payload(payload);

  console.error('[WS] Received error message: ', payload.message());

  toastDelegator.trigger({
    message: 'Error: ' + payload.message(),
    background: 'bg-red-500',
  });
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiScanStatusMessage] = (cli, msg) => {
  const payload = new WifiScanStatusMessage();
  msg.payload(payload);

  DeviceStateStore.setWifiScanStatus(payload.status());
};

PayloadHandlers[DeviceToLocalMessagePayload.WifiNetworkEvent] = WifiNetworkEventHandler;

PayloadHandlers[DeviceToLocalMessagePayload.AccountLinkCommandResult] = (cli, msg) => {
  const payload = new AccountLinkCommandResult();
  msg.payload(payload);

  const result = payload.result();

  if (result == AccountLinkResultCode.Success) {
    toastDelegator.trigger({
      message: 'Account linked successfully',
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
      message: 'Failed to link account: ' + reason,
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
  if (payloadType < 0 || payloadType >= PayloadHandlers.length) {
    console.error('[WS] ERROR: Received unknown payload type (out of bounds)');
    return;
  }

  PayloadHandlers[payloadType](cli, msg);
}
