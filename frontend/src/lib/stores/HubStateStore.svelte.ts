import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
import type { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import type { Config } from '$lib/mappers/ConfigMapper';
import type { WiFiNetwork, WiFiNetworkGroup } from '$lib/types';

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
  const key = `${value.ssid || value.bssid}_${WifiAuthMode[value.security]}`;

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
  wifiNetworks = $state<Map<string, WiFiNetwork>>(new Map());
  wifiNetworkGroups = $derived.by<Map<string, WiFiNetworkGroup>>(() =>
    Array.from(this.wifiNetworks.entries()).reduce(ssidMapReducer, new Map())
  );
  accountLinked = $state(false);
  config = $state<Config | null>(null);
  gpioValidInputs = $state<Int8Array>(new Int8Array());
  gpioValidOutputs = $state<Int8Array>(new Int8Array());

  setWifiNetwork(network: WiFiNetwork) {
    this.wifiNetworks.set(network.bssid, network);
  }

  updateWifiNetwork(bssid: string, updater: (network: WiFiNetwork) => WiFiNetwork) {
    const network = this.wifiNetworks.get(bssid);
    if (network) {
      this.wifiNetworks.set(bssid, updater(network));
    }
  }

  removeWifiNetwork(bssid: string) {
    this.wifiNetworks.delete(bssid);
  }

  clearWifiNetworks() {
    this.wifiNetworks.clear();
  }
}

export const hubState = new HubStateStore();
