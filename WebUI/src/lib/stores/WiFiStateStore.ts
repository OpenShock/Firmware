import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import type { WiFiState } from '$lib/types/WiFiState';
import { writable } from 'svelte/store';

const { subscribe, update } = writable<WiFiState>({
  initialized: false,
  connected: null,
  scanning: false,
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
  setScanning(scanning: boolean) {
    update((store) => {
      store.scanning = scanning;
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
