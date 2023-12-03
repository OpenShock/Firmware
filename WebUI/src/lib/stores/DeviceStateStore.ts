import type { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import type { DeviceState } from '$lib/types/DeviceState';
import { writable } from 'svelte/store';

const { subscribe, update } = writable<DeviceState>({
  wsConnected: false,
  deviceIPAddress: null,
  wifiScanStatus: null,
  wifiNetworksSaved: [],
  wifiNetworksPresent: new Map<string, WiFiNetwork>(),
  wifiConnectedBSSID: null,
  accountLinked: false,
  rfTxPin: null,
});

export const DeviceStateStore = {
  subscribe,
  update,
  setWsConnected(connected: boolean) {
    update((store) => {
      store.wsConnected = connected;
      return store;
    });
  },
  setDeviceIPAddress(ipAddress: string | null) {
    update((store) => {
      store.deviceIPAddress = ipAddress;
      return store;
    });
  },
  setWifiScanStatus(scanStatus: WifiScanStatus | null) {
    update((store) => {
      store.wifiScanStatus = scanStatus;
      return store;
    });
  },
  setSavedWifiNetworks(networkSSIDs: string[]) {
    update((store) => {
      store.wifiNetworksSaved = networkSSIDs;
      return store;
    });
  },
  addSavedWifiNetwork(networkSSID: string) {
    update((store) => {
      store.wifiNetworksSaved.push(networkSSID);
      return store;
    });
  },
  removeSavedWifiNetwork(networkSSID: string) {
    update((store) => {
      const index = store.wifiNetworksSaved.indexOf(networkSSID);
      if (index !== -1) {
        store.wifiNetworksSaved.splice(index, 1);
      }
      return store;
    });
  },
  clearSavedWifiNetworks() {
    update((store) => {
      store.wifiNetworksSaved = [];
      return store;
    });
  },
  setWifiNetwork(network: WiFiNetwork) {
    update((store) => {
      store.wifiNetworksPresent.set(network.bssid, network);
      return store;
    });
  },
  updateWifiNetwork(bssid: string, updater: (network: WiFiNetwork) => WiFiNetwork) {
    update((store) => {
      const network = store.wifiNetworksPresent.get(bssid);
      if (network) {
        store.wifiNetworksPresent.set(bssid, updater(network));
      }
      return store;
    });
  },
  removeWifiNetwork(bssid: string) {
    update((store) => {
      store.wifiNetworksPresent.delete(bssid);
      return store;
    });
  },
  clearWifiNetworks() {
    update((store) => {
      store.wifiNetworksPresent.clear();
      return store;
    });
  },
  setWifiConnectedBSSID(connectedBSSID: string | null) {
    update((store) => {
      store.wifiConnectedBSSID = connectedBSSID;
      return store;
    });
  },
  setAccountLinked(linked: boolean) {
    update((store) => {
      store.accountLinked = linked;
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
