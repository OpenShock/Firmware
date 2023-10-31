import type { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import type { WiFiState } from '$lib/types/WiFiState';
import { writable } from 'svelte/store';

const { subscribe, update } = writable<WiFiState>({
  wifiConnectedBSSID: null,
  wifiScanStatus: null,
  wifiNetworks: new Map<string, WiFiNetwork>(),
  gatewayPaired: false,
  rfTxPin: null,
});

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
  addWifiNetwork(network: WiFiNetwork) {
    update((store) => {
      store.wifiNetworks.set(network.bssid, network);
      return store;
    });
  },
  removeWifiNetwork(bssid: string) {
    update((store) => {
      store.wifiNetworks.delete(bssid);
      return store;
    });
  },
  clearWifiNetworks() {
    update((store) => {
      store.wifiNetworks.clear();
      return store;
    });
  },
};
