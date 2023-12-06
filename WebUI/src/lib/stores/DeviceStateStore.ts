import type { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import type { WiFiNetwork, WiFiNetworkGroup, DeviceState } from '$lib/types';
import { writable } from 'svelte/store';
import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';

const { subscribe, update } = writable<DeviceState>({
  wifiConnectedBSSID: null,
  wifiScanStatus: null,
  wifiNetworks: new Map<string, WiFiNetwork>(),
  wifiNetworkGroups: new Map<string, WiFiNetworkGroup>(),
  gatewayPaired: false,
  rfTxPin: null,
});

function insertSorted<T>(array: T[], value: T, compare: (a: T, b: T) => number) {
  let low = 0,
    high = array.length;
  while (low < high) {
    const mid = (low + high) >>> 1;
    if (compare(array[mid], value) < 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  array.splice(low, 0, value);
}

function SsidMapReducer(groups: Map<string, WiFiNetworkGroup>, [_, value]: [string, WiFiNetwork]): Map<string, WiFiNetworkGroup> {
  const key = `${value.ssid || value.bssid}_${WifiAuthMode[value.security]}`;

  // Get the group for this SSID, or create a new one
  const group = groups.get(key) ?? ({ ssid: value.ssid, saved: false, security: value.security, networks: [] } as WiFiNetworkGroup);

  // Update the group's saved status
  group.saved = group.saved || value.saved;

  // Add the network to the group, sorted by signal strength (RSSI, higher is stronger)
  insertSorted(group.networks, value, (a, b) => b.rssi - a.rssi);

  // Update the group in the map
  groups.set(key, group);

  // Return the updated groups object
  return groups;
}

function updateWifiNetworkGroups(store: DeviceState) {
  store.wifiNetworkGroups = Array.from(store.wifiNetworks.entries()).reduce(SsidMapReducer, new Map<string, WiFiNetworkGroup>());
  // Sort the groups themselves by each one's strongest network (by RSSI, higher is stronger)
  // Only need to check the first network in each group, since they're already sorted by signal strength
  store.wifiNetworkGroups = new Map(
    Array.from(store.wifiNetworkGroups.entries()).sort(([_, a], [__, b]) => b.networks[0].rssi - a.networks[0].rssi)
  );
}

export const DeviceStateStore = {
  subscribe,
  update,
  setWifiConnectedBSSID(connectedBSSID: string | null) {
    update((store) => {
      store.wifiConnectedBSSID = connectedBSSID;
      return store;
    });
  },
  setWifiScanStatus(scanStatus: WifiScanStatus | null) {
    update((store) => {
      store.wifiScanStatus = scanStatus;
      return store;
    });
  },
  setWifiNetwork(network: WiFiNetwork) {
    update((store) => {
      store.wifiNetworks.set(network.bssid, network);
      updateWifiNetworkGroups(store);
      return store;
    });
  },
  updateWifiNetwork(bssid: string, updater: (network: WiFiNetwork) => WiFiNetwork) {
    update((store) => {
      const network = store.wifiNetworks.get(bssid);
      if (network) {
        store.wifiNetworks.set(bssid, updater(network));
        updateWifiNetworkGroups(store);
      }
      return store;
    });
  },
  removeWifiNetwork(bssid: string) {
    update((store) => {
      store.wifiNetworks.delete(bssid);
      updateWifiNetworkGroups(store);
      return store;
    });
  },
  clearWifiNetworks() {
    update((store) => {
      store.wifiNetworks.clear();
      store.wifiNetworkGroups.clear();
      return store;
    });
  },
  setGatewayPaired(paired: boolean) {
    update((store) => {
      store.gatewayPaired = paired;
      return store;
    });
  },
  setRfTxPin(pin: number) {
    update((store) => {
      store.rfTxPin = pin;
      return store;
    });
  },
};
