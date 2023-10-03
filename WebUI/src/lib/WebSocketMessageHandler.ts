import { WiFiStateStore } from './stores';
import type { WiFiNetwork } from './types/WiFiNetwork';

interface WiFiMessage {
  type: 'wifi';
  subject: 'scan';
}

interface WiFiScanMessage extends WiFiMessage {
  subject: 'scan';
  status: 'started' | 'completed' | 'error';
  networks?: WiFiNetwork[];
}

interface InvalidMessage {
  type: undefined | null;
}

export type WebSocketMessage = InvalidMessage | WiFiScanMessage;

export function WebSocketMessageHandler(message: WebSocketMessage) {
  const type = message.type;
  if (!type) {
    console.warn('[WS] Received invalid message: ', message);
    return;
  }

  switch (type) {
    case 'wifi':
      handleWiFiMessage(message);
      break;
    default:
      console.warn('[WS] Received invalid message: ', message);
      return;
  }
}

function handleWiFiMessage(message: WiFiScanMessage) {
  switch (message.subject) {
    case 'scan':
      handleWiFiScanMessage(message);
      break;
    default:
      console.warn('[WS] Received invalid wifi message: ', message);
      return;
  }
}

function handleWiFiScanMessage(message: WiFiScanMessage) {
  let running: boolean;
  switch (message.status) {
    case 'started':
      running = true;
      break;
    case 'completed':
      running = false;
      break;
    case 'error':
      running = false;
      break;
    default:
      console.warn('[WS] Received invalid scan message: ', message);
      return;
  }

  WiFiStateStore.setScanning(running);
  if (message.networks) {
    WiFiStateStore.setNetworks(message.networks);
  }
}
