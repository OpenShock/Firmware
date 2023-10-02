import { getToastStore } from "@skeletonlabs/skeleton";
import { WiFiStateStore } from "./stores";
import type { WiFiNetwork } from "./types/WiFiNetwork";

interface ScanMessage {
  type: 'scan';
  status: 'started' | 'finished';
  networks?: WiFiNetwork[];
}

interface InvalidMessage {
  type: undefined | null;
}

export type WebSocketMessage = InvalidMessage | ScanMessage;

export function WebSocketMessageHandler(message: WebSocketMessage) {
  const type = message.type;
  if (!type) {
    console.warn('[WS] Received invalid message: ', message);
    return;
  }

  switch (type) {
    case 'scan':
      handleScanMessage(message);
      break;
    default:
      console.warn('[WS] Received invalid message: ', message);
      return;
  }
}

function handleScanMessage(message: ScanMessage) {
  const toastStore = getToastStore();
  switch (message.status) {
    case 'started':
      toastStore.trigger({ message: 'Scanning for WiFi networks...', background: 'bg-blue-500' });
      break;
    case 'finished':
      toastStore.trigger({ message: 'Scanning for WiFi networks finished', background: 'bg-green-500' });
      break;
    default:
      console.warn('[WS] Received invalid scan message: ', message);
      return;
  }

  WiFiStateStore.setScanning(message.status === 'started');
  if (message.networks) {
    WiFiStateStore.setNetworks(message.networks);
  }
}
