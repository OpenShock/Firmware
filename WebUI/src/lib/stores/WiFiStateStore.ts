import type { WifiScanStatus } from '$lib/_fbs/open-shock';
import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import type { WiFiState } from '$lib/types/WiFiState';
import { writable } from 'svelte/store';

const { subscribe, update } = writable<WiFiState>({
  initialized: false,
  connected: null,
  scan_status: null,
  networks: new Map<string, WiFiNetwork>(),
});

export const WiFiStateStore = {
  subscribe,
  setInitialized(initialized: boolean) {
    update((store) => {
      store.initialized = initialized;
      return store;
    });
  },
  setConnected(connected: string | null) {
    update((store) => {
      store.connected = connected;
      return store;
    });
  },
  setScanStatus(scanStatus: WifiScanStatus | null) {
    update((store) => {
      store.scan_status = scanStatus;
      return store;
    });
  },
  addNetwork(network: WiFiNetwork) {
    update((store) => {
      store.networks.set(network.bssid, network);
      return store;
    });
  },
  removeNetwork(bssid: string) {
    update((store) => {
      store.networks.delete(bssid);
      return store;
    });
  },
  clearNetworks() {
    update((store) => {
      store.networks.clear();
      return store;
    });
  },
};
