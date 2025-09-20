import { WifiNetworkEvent } from '$lib/_fbs/open-shock/serialization/local/wifi-network-event';
import { WifiNetwork as FbsWifiNetwork } from '$lib/_fbs/open-shock/serialization/types/wifi-network';
import { WifiNetworkEventType } from '$lib/_fbs/open-shock/serialization/types/wifi-network-event-type';
import { HubStateStore } from '$lib/stores';
import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import { toast } from 'svelte-sonner';
import type { MessageHandler } from '.';

function handleInvalidEvent() {
  console.warn('[WS] Received invalid event type');
}
function handleDiscoveredEvent(fbsNetwork: FbsWifiNetwork) {
  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network discovered event');
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

  HubStateStore.setWifiNetwork(network);
}
function handleUpdatedEvent(fbsNetwork: FbsWifiNetwork) {
  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network updated event');
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

  HubStateStore.setWifiNetwork(network);
}
function handleLostEvent(fbsNetwork: FbsWifiNetwork) {
  const bssid = fbsNetwork.bssid();

  if (!bssid) {
    console.warn('[WS] Received invalid network lost event');
    return;
  }

  HubStateStore.removeWifiNetwork(bssid);
}
function handleSavedEvent(fbsNetwork: FbsWifiNetwork) {
  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network saved event');
    return;
  }

  HubStateStore.updateWifiNetwork(bssid, (network) => {
    network.saved = true;
    return network;
  });

  toast.success('WiFi network saved: ' + ssid);
}
function handleRemovedEvent(fbsNetwork: FbsWifiNetwork) {
  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network forgotten event');
    return;
  }

  HubStateStore.updateWifiNetwork(bssid, (network) => {
    network.saved = false;
    return network;
  });

  toast.success('WiFi network forgotten: ' + ssid);
}
function handleConnectedEvent(fbsNetwork: FbsWifiNetwork) {
  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network connected event');
    return;
  }

  HubStateStore.setWifiConnectedBSSID(bssid);

  toast.info('WiFi network connected: ' + ssid);
}
function handleDisconnectedEvent(fbsNetwork: FbsWifiNetwork) {
  const ssid = fbsNetwork.ssid();
  const bssid = fbsNetwork.bssid();

  if (!ssid || !bssid) {
    console.warn('[WS] Received invalid network disconnected event');
    return;
  }

  HubStateStore.setWifiConnectedBSSID(null);

  toast.info('WiFi network disconnected: ' + ssid);
}

type WifiNetworkEventHandler = (fbsNetwork: FbsWifiNetwork) => void;

const EventTypes = Object.keys(WifiNetworkEventType).length / 2;
const EventHandlers: WifiNetworkEventHandler[] = new Array<WifiNetworkEventHandler>(
  EventTypes
).fill(handleInvalidEvent);

EventHandlers[WifiNetworkEventType.Discovered] = handleDiscoveredEvent;
EventHandlers[WifiNetworkEventType.Updated] = handleUpdatedEvent;
EventHandlers[WifiNetworkEventType.Lost] = handleLostEvent;
EventHandlers[WifiNetworkEventType.Saved] = handleSavedEvent;
EventHandlers[WifiNetworkEventType.Removed] = handleRemovedEvent;
EventHandlers[WifiNetworkEventType.Connected] = handleConnectedEvent;
EventHandlers[WifiNetworkEventType.Disconnected] = handleDisconnectedEvent;

export const WifiNetworkEventHandler: MessageHandler = (cli, msg) => {
  const payload = new WifiNetworkEvent();
  msg.payload(payload);

  const eventType = payload.eventType();

  if (eventType < 0 || eventType >= EventHandlers.length) {
    console.warn('[WS] Received invalid wifi network event type (out of bounds)');
    return;
  }

  const networksLength = payload.networksLength();
  for (let i = 0; i < networksLength; i++) {
    const fbsNetwork = payload.networks(i);
    if (!fbsNetwork) {
      console.warn('[WS] Received invalid wifi network event (null network)');
      continue;
    }
    EventHandlers[eventType](fbsNetwork);
  }
};
