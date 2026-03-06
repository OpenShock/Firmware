import { browser } from '$app/environment';
import { isString } from '$lib/typeguards';

export enum ColorScheme {
  Dark = 'dark',
  Light = 'light',
  System = 'system',
}

function isColorSchemeEnum(value: unknown): value is ColorScheme {
  if (!isString(value)) return false;
  return Object.values(ColorScheme).includes(value as ColorScheme);
}

function getLocalStoreState(): ColorScheme {
  // If we are not in a browser environment, return default
  if (!browser) {
    return ColorScheme.System;
  }

  // If the stored value is invalid, reset it and return default
  const scheme = localStorage.getItem('theme');
  if (!isColorSchemeEnum(scheme)) {
    localStorage.setItem('theme', ColorScheme.System);
    return ColorScheme.System;
  }

  return scheme;
}

export function getDarkReaderState() {
  const rootHtml = document.documentElement;

  const proxyInjected = rootHtml.getAttribute('data-darkreader-proxy-injected');
  const metaElement = rootHtml.querySelector('head meta[name="darkreader"]');
  const scheme = rootHtml.getAttribute('data-darkreader-scheme');

  return {
    isInjected: proxyInjected === 'true',
    isActive: metaElement !== null,
    scheme,
  };
}

function resolveDarkMode(userPreference: ColorScheme): boolean {
  if (userPreference !== ColorScheme.System) {
    return userPreference !== ColorScheme.Light;
  }

  // If a user has Dark Reader extension installed, assume they prefer dark mode
  const darkReaderState = getDarkReaderState();
  if (darkReaderState.isInjected) {
    return true;
  }

  // Check if the user has a system theme preference for light mode
  if (window.matchMedia('(prefers-color-scheme: light)').matches) {
    return false;
  }

  // Default to dark mode
  return true;
}

function setDarkMode(preference: ColorScheme) {
  document.documentElement.classList.toggle('dark', resolveDarkMode(preference));
}

class ColorSchemeState {
  #value;

  constructor() {
    this.#value = $state<ColorScheme>(getLocalStoreState());
  }

  get Value() {
    return this.#value;
  }
  set Value(value: ColorScheme) {
    localStorage.setItem('theme', value);
    setDarkMode(value);
  }
}

export const colorScheme = new ColorSchemeState();

function handleMediaQueryChange() {
  setDarkMode(getLocalStoreState());
}
export function initializeDarkModeStore() {
  handleMediaQueryChange();

  window
    .matchMedia('(prefers-color-scheme: light)')
    .addEventListener('change', handleMediaQueryChange);
  window
    .matchMedia('(prefers-color-scheme: dark)')
    .addEventListener('change', handleMediaQueryChange);

  window.addEventListener('storage', (event) => {
    if (event.key !== 'theme') return;

    setDarkMode(isColorSchemeEnum(event.newValue) ? event.newValue : ColorScheme.System);
  });
}
