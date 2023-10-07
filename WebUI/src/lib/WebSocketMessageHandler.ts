import { WebSocketClient } from './WebSocketClient';
import { WiFiStateStore } from './stores';
import type { WiFiNetwork } from './types/WiFiNetwork';

interface InvalidMessage {
  type: undefined | null;
}

interface PoggiesMessage {
  type: 'poggies';
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

interface WiFiScanCancelledMessage {
  type: 'wifi';
  subject: 'scan';
  status: 'cancelled';
}

interface WiFiScanErrorMessage {
  type: 'wifi';
  subject: 'scan';
  status: 'error';
}

interface WiFiConnectSuccessMessage {
  type: 'wifi';
  subject: 'connect';
  status: 'success';
  ssid: string;
}

interface WiFiConnectErrorMessage {
  type: 'wifi';
  subject: 'connect';
  status: 'error';
  ssid: string;
  bssid: string;
  error: string;
}

export type WiFiScanMessage = WiFiScanStartedMessage | WiFiScanDiscoveryMessage | WiFiScanCompletedMessage | WiFiScanCancelledMessage | WiFiScanErrorMessage;
export type WiFiConnectMessage = WiFiConnectSuccessMessage | WiFiConnectErrorMessage;
export type WiFiMessage = WiFiScanMessage | WiFiConnectMessage;
export type WebSocketMessage = InvalidMessage | PoggiesMessage | WiFiMessage;

export function WebSocketMessageHandler(message: WebSocketMessage) {
  const type = message.type;
  if (!type) {
    console.warn('[WS] Received invalid message: ', message);
    return;
  }

  switch (type) {
    case 'poggies':
      handlePoggiesMessage();
      break;
    case 'wifi':
      handleWiFiMessage(message);
      break;
    default:
      console.warn('[WS] Received invalid message: ', message);
      return;
  }
}

function handlePoggiesMessage() {
  WebSocketClient.Instance.Send('{ "type": "wifi", "action": "scan", "run": true }');
}

function handleWiFiMessage(message: WiFiMessage) {
  switch (message.subject) {
    case 'scan':
      handleWiFiScanMessage(message);
      break;
    case 'connect':
      handleWiFiConnectMessage(message);
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
    case 'cancelled':
      handleWiFiScanCancelledMessage();
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

function handleWiFiScanCancelledMessage() {
  WiFiStateStore.setScanning(false);
}

function handleWiFiScanErrorMessage(message: WiFiScanErrorMessage) {
  console.error('[WS] Received WiFi scan error message: ', message);
  WiFiStateStore.setScanning(false);
}

function handleWiFiConnectMessage(message: WiFiConnectMessage) {
  switch (message.status) {
    case 'success':
      handleWiFiConnectSuccessMessage(message);
      break;
    case 'error':
      handleWiFiConnectErrorMessage(message);
      break;
    default:
      console.warn('[WS] Received invalid connect message: ', message);
      return;
  }
}

function handleWiFiConnectSuccessMessage(message: WiFiConnectSuccessMessage) {
  WiFiStateStore.setConnected(message.ssid);
}

function handleWiFiConnectErrorMessage(message: WiFiConnectErrorMessage) {
  console.error('[WS] Failed to connect to %s (%s): %s', message.ssid, message.bssid, message.error);
  WiFiStateStore.setConnected(null);
}
