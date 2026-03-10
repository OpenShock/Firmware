import { writable } from 'svelte/store';

type ViewMode = 'wizard' | 'advanced';

function getInitialMode(): ViewMode {
  const stored = localStorage.getItem('viewMode');
  if (stored === 'wizard' || stored === 'advanced') return stored;
  return 'wizard';
}

const { set, subscribe } = writable<ViewMode>(getInitialMode());

export const ViewModeStore = {
  set: (value: ViewMode) => {
    localStorage.setItem('viewMode', value);
    set(value);
  },
  toggle: () => {
    let current: ViewMode = 'wizard';
    const unsub = subscribe((v) => (current = v));
    unsub();
    const next: ViewMode = current === 'wizard' ? 'advanced' : 'wizard';
    localStorage.setItem('viewMode', next);
    set(next);
  },
  subscribe,
};
