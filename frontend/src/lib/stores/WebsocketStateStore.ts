import { writable } from 'svelte/store';
import type { WebsocketState } from '$lib/types/WebsocketState';

const { subscribe, update } = writable<WebsocketState>({
  connected: false,
});

export const WebsocketStateStore = {
  subscribe,
  update,
  setConnected(connected: boolean) {
    update((store) => {
      store.connected = connected;
      return store;
    });
  },
};
