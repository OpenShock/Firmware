import { WifiAuthMode } from '#lib/_fbs/open-shock/serialization/types/wifi-auth-mode.js';
import type { WifiScanStatus } from '#lib/_fbs/open-shock/serialization/types/wifi-scan-status.js';
import type { Config } from '#lib/mappers/ConfigMapper.js';
import type { WiFiNetwork, WiFiNetworkGroup } from '#lib/types/index.js';
import { SvelteMap } from 'svelte/reactivity';

function insertSorted<T>(array: T[], value: T, compare: (a: T, b: T) => number) {
  let low = 0,
    high = array.length;
  while (low < high) {
    const mid = (low + high) >>> 1;
    if (compare(array[mid], value) < 0) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  array.splice(low, 0, value);
}

function ssidMapReducer(
  groups: Map<string, WiFiNetworkGroup>,
  [, value]: [string, WiFiNetwork]
): Map<string, WiFiNetworkGroup> {
  const key = `${value.ssid}_${WifiAuthMode[value.security]}`;

  const group =
    groups.get(key) ??
    ({
      ssid: value.ssid,
      saved: false,
      security: value.security,
      networks: [],
    } as WiFiNetworkGroup);

  group.saved = group.saved || value.saved;
  insertSorted(group.networks, value, (a, b) => b.rssi - a.rssi);
  groups.set(key, group);
  return groups;
}

class HubStateStore {
  wifiConnectedBSSID = $state<string | null>(null);
  wifiScanStatus = $state<WifiScanStatus | null>(null);
  wifiNetworks = new SvelteMap<string, WiFiNetwork>();
  // Rebuilt from scratch on every change, so plain (non-reactive) collections are enough here
  wifiNetworkGroups = $derived.by<Map<string, WiFiNetworkGroup>>(() =>
    Array.from(this.wifiNetworks.entries()).reduce(ssidMapReducer, new Map())
  );
  // Saved SSIDs from config that aren't visible in scan results
  savedOnlySSIDs = $derived.by<string[]>(() => {
    const scannedSavedSSIDs = new Set<string>();
    for (const [, group] of this.wifiNetworkGroups) {
      if (group.saved) scannedSavedSSIDs.add(group.ssid);
    }
    const creds = this.config?.wifi?.credentials ?? [];
    return creds.map((c) => c.ssid).filter((ssid) => !scannedSavedSSIDs.has(ssid));
  });

  accountLinked = $state(false);
  hasPredefinedPins = $state(false);
  config = $state<Config | null>(null);
  gpioValidInputs = $state<Int8Array>(new Int8Array());
  gpioValidOutputs = $state<Int8Array>(new Int8Array());

  // BSSIDs only listed because the hub is connected to them, not because a scan found them
  #connectionOnlyBSSIDs = new Set<string>();

  setWifiNetwork(network: WiFiNetwork) {
    this.#connectionOnlyBSSIDs.delete(network.bssid);
    this.wifiNetworks.set(network.bssid, network);
  }

  // The connected network may not be part of the scan results (e.g. connected on boot before any scan),
  // make sure it is listed while connected so the UI can display it
  setConnectedWifiNetwork(network: WiFiNetwork | null) {
    const previousBSSID = this.wifiConnectedBSSID;
    if (previousBSSID && previousBSSID !== network?.bssid) {
      if (this.#connectionOnlyBSSIDs.delete(previousBSSID)) {
        this.wifiNetworks.delete(previousBSSID);
      }
    }

    if (network && !this.wifiNetworks.has(network.bssid)) {
      this.wifiNetworks.set(network.bssid, network);
      this.#connectionOnlyBSSIDs.add(network.bssid);
    }

    this.wifiConnectedBSSID = network?.bssid ?? null;
  }

  // Network objects are replaced, never mutated: SvelteMap only notifies when the stored value changes identity.
  markWifiNetworksSaved(ssid: string) {
    this.#setWifiNetworksSaved(ssid, true);
  }

  markWifiNetworksUnsaved(ssid: string) {
    this.#setWifiNetworksSaved(ssid, false);
  }

  #setWifiNetworksSaved(ssid: string, saved: boolean) {
    for (const [bssid, network] of this.wifiNetworks) {
      if (network.ssid === ssid && network.saved !== saved) {
        this.wifiNetworks.set(bssid, { ...network, saved });
      }
    }
  }

  removeWifiNetwork(bssid: string) {
    this.#connectionOnlyBSSIDs.delete(bssid);
    this.wifiNetworks.delete(bssid);
  }

  clearWifiNetworks() {
    this.#connectionOnlyBSSIDs.clear();
    this.wifiNetworks.clear();
  }
}

export const hubState = new HubStateStore();
