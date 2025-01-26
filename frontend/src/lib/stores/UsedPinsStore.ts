import { writable } from 'svelte/store';

const { subscribe, update } = writable<Map<number, string>>(new Map<number, string>());

export const UsedPinsStore = {
  subscribe,
  markPinUsed(pin: number, name: string) {
    update((store) => {
      // Remove any existing entries with the same pin or name
      for (const [key, value] of store) {
        if (key === pin || value === name) {
          store.delete(key);
        }
      }

      store.set(pin, name);

      return store;
    });
  },
};
