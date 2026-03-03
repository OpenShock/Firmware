import { browser } from '$app/environment';
import { writable } from 'svelte/store';

type ViewMode = 'wizard' | 'advanced';

function getInitialMode(): ViewMode {
  if (!browser) return 'wizard';
  const stored = localStorage.getItem('viewMode');
  if (stored === 'wizard' || stored === 'advanced') return stored;
  return 'wizard';
}

const { set, subscribe } = writable<ViewMode>(getInitialMode());

export const ViewModeStore = {
  set: (value: ViewMode) => {
    if (browser) localStorage.setItem('viewMode', value);
    set(value);
  },
  toggle: () => {
    let current: ViewMode = 'wizard';
    const unsub = subscribe((v) => (current = v));
    unsub();
    const next: ViewMode = current === 'wizard' ? 'advanced' : 'wizard';
    if (browser) localStorage.setItem('viewMode', next);
    set(next);
  },
  subscribe,
};
