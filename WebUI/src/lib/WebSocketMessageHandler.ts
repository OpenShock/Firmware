import { WiFiStateStore } from './stores';
import type { WiFiNetwork } from './types/WiFiNetwork';

interface WiFiMessage {
  type: 'wifi';
  subject: 'scan';
}

interface WiFiScanMessage extends WiFiMessage {
  subject: 'scan';
  status: 'started' | 'discovery' | 'completed' | 'error';
  data?: WiFiNetwork;
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
  switch (message.status) {
    case 'started':
      handleWiFiScanStartedMessage(message);
      break;
    case 'discovery':
      handleWiFiScanDiscoveryMessage(message);
      break;
    case 'completed':
      handleWiFiScanCompletedMessage(message);
      break;
    case 'error':
      handleWiFiScanErrorMessage(message);
      break;
    default:
      console.warn('[WS] Received invalid scan message: ', message);
      return;
  }
}

function handleWiFiScanStartedMessage(message: WiFiScanMessage) {
  WiFiStateStore.setScanning(true);
}

function handleWiFiScanDiscoveryMessage(message: WiFiScanMessage) {
  WiFiStateStore.addNetwork(message.data!);
}

function handleWiFiScanCompletedMessage(message: WiFiScanMessage) {
  WiFiStateStore.setScanning(false);
}

function handleWiFiScanErrorMessage(message: WiFiScanMessage) {
  WiFiStateStore.setScanning(false);
}
