import type { ToastSettings } from '@skeletonlabs/skeleton';
import { writable } from 'svelte/store';

export type DelegatedAction =
  | {
      type: 'trigger';
      toast: ToastSettings;
    }
  | {
      type: 'clear';
    };

const store = writable<DelegatedAction[]>([]);

export const toastDelegator = {
  subscribe: store.subscribe,
  trigger(toast: ToastSettings) {
    store.update((toasts) => {
      toasts.push({ type: 'trigger', toast });
      return toasts;
    });
  },
  clear() {
    store.set([{ type: 'clear' }]);
  },
  set(toasts: DelegatedAction[]) {
    store.set(toasts);
  },
};
