import { WifiAuthMode } from '#lib/_fbs/open-shock/serialization/types/wifi-auth-mode.js';
import type { WiFiNetwork } from '#lib/types/index.js';
import { describe, expect, it } from 'vitest';
import { HubStateStore } from './HubStateStore.svelte';
import { UsedPinsStore } from './UsedPinsStore.svelte';

function makeNetwork(overrides: Partial<WiFiNetwork> = {}): WiFiNetwork {
  return {
    ssid: 'TestNet',
    bssid: 'AA:BB:CC:DD:EE:FF',
    rssi: -60,
    channel: 6,
    security: WifiAuthMode.WPA2_PSK,
    saved: false,
    ...overrides,
  };
}

describe('UsedPinsStore', () => {
  it('reports a pin as not used initially', () => {
    expect(new UsedPinsStore().has(4)).toBe(false);
  });

  it('marks a pin as used', () => {
    const store = new UsedPinsStore();
    store.markPinUsed(4, 'RF TX');
    expect(store.has(4)).toBe(true);
  });

  it('removes the old pin when the same name moves to another pin', () => {
    const store = new UsedPinsStore();
    store.markPinUsed(4, 'RF TX');
    store.markPinUsed(5, 'RF TX');
    expect(store.has(4)).toBe(false);
    expect(store.has(5)).toBe(true);
  });

  it('tracks multiple distinct pins independently', () => {
    const store = new UsedPinsStore();
    store.markPinUsed(4, 'RF TX');
    store.markPinUsed(12, 'EStop');
    expect(store.has(4)).toBe(true);
    expect(store.has(12)).toBe(true);
    expect(store.has(99)).toBe(false);
  });
});

describe('HubStateStore WiFi grouping', () => {
  it('groups BSSIDs of one SSID and security, strongest first', () => {
    const store = new HubStateStore();
    store.setWifiNetwork(makeNetwork({ bssid: '01:00:00:00:00:00', rssi: -80 }));
    store.setWifiNetwork(makeNetwork({ bssid: '02:00:00:00:00:00', rssi: -40 }));
    store.setWifiNetwork(makeNetwork({ bssid: '03:00:00:00:00:00', ssid: 'Other' }));

    const groups = Array.from(store.wifiNetworkGroups.values());
    expect(groups).toHaveLength(2);

    const testNet = groups.find((g) => g.ssid === 'TestNet');
    expect(testNet?.networks.map((n) => n.rssi)).toEqual([-40, -80]);
  });

  it('marks every BSSID of an SSID as saved, replacing the stored objects', () => {
    const store = new HubStateStore();
    const original = makeNetwork({ bssid: '01:00:00:00:00:00' });
    store.setWifiNetwork(original);
    store.setWifiNetwork(makeNetwork({ bssid: '02:00:00:00:00:00' }));

    store.markWifiNetworksSaved('TestNet');

    expect(store.wifiNetworks.get('01:00:00:00:00:00')?.saved).toBe(true);
    expect(store.wifiNetworks.get('02:00:00:00:00:00')?.saved).toBe(true);
    // A new object, so the reactive map notifies (mutating in place did not)
    expect(store.wifiNetworks.get('01:00:00:00:00:00')).not.toBe(original);
    expect(original.saved).toBe(false);

    store.markWifiNetworksUnsaved('TestNet');
    expect(store.wifiNetworks.get('01:00:00:00:00:00')?.saved).toBe(false);
  });

  it('lists the connected network even when it was not scanned, until it disconnects', () => {
    const store = new HubStateStore();
    const connected = makeNetwork({ bssid: '09:00:00:00:00:00' });

    store.setConnectedWifiNetwork(connected);
    expect(store.wifiNetworks.has('09:00:00:00:00:00')).toBe(true);

    store.setConnectedWifiNetwork(null);
    expect(store.wifiNetworks.has('09:00:00:00:00:00')).toBe(false);
  });

  it('clearWifiNetworks removes everything', () => {
    const store = new HubStateStore();
    store.setWifiNetwork(makeNetwork());
    store.clearWifiNetworks();
    expect(store.wifiNetworks.size).toBe(0);
  });
});
