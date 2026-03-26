import { writable } from 'svelte/store';

export type ViewMode = 'landing' | 'wizard' | 'advanced';

const { set, subscribe } = writable<ViewMode>('landing');

export const ViewModeStore = {
  set: (value: ViewMode) => {
    set(value);
  },
  subscribe,
};
