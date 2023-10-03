import { WiFiStateStore } from './stores';
import type { WiFiNetwork } from './types/WiFiNetwork';

interface InvalidMessage {
  type: undefined | null;
}

interface WiFiScanStartedMessage {
  type: 'wifi';
  subject: 'scan';
  status: 'started';
}

interface WiFiScanDiscoveryMessage {
  type: 'wifi';
  subject: 'scan';
  status: 'discovery';
  data: WiFiNetwork;
}

interface WiFiScanCompletedMessage {
  type: 'wifi';
  subject: 'scan';
  status: 'completed';
}

interface WiFiScanErrorMessage {
  type: 'wifi';
  subject: 'scan';
  status: 'error';
}

export type WiFiScanMessage = WiFiScanStartedMessage | WiFiScanDiscoveryMessage | WiFiScanCompletedMessage | WiFiScanErrorMessage;
export type WiFiMessage = WiFiScanMessage;
export type WebSocketMessage = InvalidMessage | WiFiMessage;

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

function handleWiFiMessage(message: WiFiMessage) {
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
      handleWiFiScanStartedMessage();
      break;
    case 'discovery':
      handleWiFiScanDiscoveryMessage(message);
      break;
    case 'completed':
      handleWiFiScanCompletedMessage();
      break;
    case 'error':
      handleWiFiScanErrorMessage(message);
      break;
    default:
      console.warn('[WS] Received invalid scan message: ', message);
      return;
  }
}

function handleWiFiScanStartedMessage() {
  WiFiStateStore.setScanning(true);
}

function handleWiFiScanDiscoveryMessage(message: WiFiScanDiscoveryMessage) {
  WiFiStateStore.addNetwork(message.data);
}

function handleWiFiScanCompletedMessage() {
  WiFiStateStore.setScanning(false);
}

function handleWiFiScanErrorMessage(message: WiFiScanErrorMessage) {
  console.error('[WS] Received WiFi scan error message: ', message);
  WiFiStateStore.setScanning(false);
}
