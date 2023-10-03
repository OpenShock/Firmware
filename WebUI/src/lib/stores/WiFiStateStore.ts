import type { WiFiNetwork } from '$lib/types/WiFiNetwork';
import type { WiFiState } from '$lib/types/WiFiState';
import { writable } from 'svelte/store';

const { subscribe, update } = writable<WiFiState>({
  initialized: false,
  scanning: false,
  networks: [],
});

export const WiFiStateStore = {
  subscribe,
  setInitialized(initialized: boolean) {
    update((store) => {
      store.initialized = initialized;
      return store;
    });
  },
  setScanning(scanning: boolean) {
    update((store) => {
      store.scanning = scanning;
      return store;
    });
  },
  setNetworks(networks: WiFiNetwork[]) {
    update((store) => {
      store.scanning = false;
      store.networks = networks;
      return store;
    });
  },
  addNetwork(network: WiFiNetwork) {
    update((store) => {
      store.networks = [...store.networks, network];
      return store;
    });
  },
  clearNetworks() {
    update((store) => {
      store.networks = [];
      return store;
    });
  },
};
