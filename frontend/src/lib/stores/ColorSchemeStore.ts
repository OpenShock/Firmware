import { browser } from '$app/environment';
import { writable, type Updater } from 'svelte/store';

function getLocalStoreState() {
  const scheme = localStorage.getItem('theme');
  if (scheme === 'dark' || scheme === 'light' || scheme === 'system') {
    return scheme;
  }

  localStorage.setItem('theme', 'system');

  return 'system';
}

export function getDarkReaderState() {
  const rootHtml = document.documentElement;

  const proxyInjected = rootHtml.getAttribute('data-darkreader-proxy-injected');
  const metaElement = rootHtml.querySelector('head meta[name="darkreader"]');

  let scheme = rootHtml.getAttribute('data-darkreader-scheme');
  if (scheme === 'auto') {
    scheme = 'system';
  }

  return {
    isInjected: proxyInjected === 'true',
    isActive: metaElement !== null,
    scheme,
  };
}

function getColorSchemePreference() {
  // If we are not in a browser environment, return default
  if (!browser) {
    return 'system';
  }

  // Check if local storage has a theme stored
  const localStoreState = getLocalStoreState();
  if (localStoreState !== 'system') {
    return localStoreState;
  }

  // If a user has Dark Reader extension installed, assume they prefer dark mode
  const darkReaderState = getDarkReaderState();
  if (darkReaderState.isInjected) {
    return 'dark';
  }

  // Check if the user has a system theme preference for light mode
  if (window.matchMedia('(prefers-color-scheme: light)').matches) {
    return 'light';
  }

  // Default to dark mode
  return 'dark';
}

const { set, update, subscribe } = writable<'dark' | 'light' | 'system'>(
  getColorSchemePreference()
);

function setHtmlDarkModeSelector(value: boolean) {
  document.documentElement.classList.toggle('dark', value);
}

function handleSchemePreferenceChange() {
  const scheme = getColorSchemePreference();
  setHtmlDarkModeSelector(scheme === 'dark');
}

export const ColorSchemeStore = {
  set: (value: 'dark' | 'light' | 'system') => {
    localStorage.setItem('theme', value);
    set(value);
    handleSchemePreferenceChange();
  },
  update: (updater: Updater<'dark' | 'light' | 'system'>) => {
    update((value) => {
      const oldValue = value;
      const newValue = updater(value);
      if (oldValue !== newValue) {
        setHtmlDarkModeSelector(newValue === 'dark');
        localStorage.setItem('theme', newValue);
      }
      return newValue;
    });
  },
  subscribe,
};

export function willActivateLightMode(value: 'dark' | 'light' | 'system') {
  return (
    value === 'light' ||
    (value === 'system' && window.matchMedia('(prefers-color-scheme: light)').matches)
  );
}

export function initializeDarkModeStore() {
  const schemePreference = getColorSchemePreference();
  setHtmlDarkModeSelector(schemePreference === 'dark');
  set(schemePreference);

  window
    .matchMedia('(prefers-color-scheme: light)')
    .addEventListener('change', handleSchemePreferenceChange);
  window
    .matchMedia('(prefers-color-scheme: dark)')
    .addEventListener('change', handleSchemePreferenceChange);

  window.addEventListener('storage', (event) => {
    if (event.key !== 'theme') return;

    setHtmlDarkModeSelector(event.newValue === 'dark');
  });
}
