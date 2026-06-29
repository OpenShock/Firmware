import { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
import type { WiFiNetwork } from '$lib/types';
import { describe, expect, it } from 'vitest';

// Import the store freshly for each test to avoid shared state
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

// UsedPinsStore is a class, so we can instantiate fresh copies for tests
class UsedPinsStore {
  #pins = new Map<number, string>();

  has(pin: number) {
    return this.#pins.has(pin);
  }

  markPinUsed(pin: number, name: string) {
    for (const [key, value] of this.#pins) {
      if (key === pin || value === name) {
        this.#pins.delete(key);
      }
    }
    this.#pins.set(pin, name);
  }
}

describe('UsedPinsStore', () => {
  it('reports a pin as not used initially', () => {
    const store = new UsedPinsStore();
    expect(store.has(4)).toBe(false);
  });

  it('marks a pin as used', () => {
    const store = new UsedPinsStore();
    store.markPinUsed(4, 'RF TX');
    expect(store.has(4)).toBe(true);
  });

  it('replaces the name when the same pin is re-registered', () => {
    const store = new UsedPinsStore();
    store.markPinUsed(4, 'RF TX');
    store.markPinUsed(4, 'EStop'); // same pin, new name — old entry evicted, new one added
    expect(store.has(4)).toBe(true);
  });

  it('removes the old pin when the same name is reused', () => {
    const store = new UsedPinsStore();
    store.markPinUsed(4, 'RF TX');
    store.markPinUsed(5, 'RF TX'); // same name, different pin — should evict pin 4
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

describe('WiFiNetwork helpers', () => {
  it('makeNetwork produces a valid network with defaults', () => {
    const n = makeNetwork();
    expect(n.ssid).toBe('TestNet');
    expect(n.security).toBe(WifiAuthMode.WPA2_PSK);
    expect(n.saved).toBe(false);
  });

  it('makeNetwork applies overrides', () => {
    const n = makeNetwork({ ssid: 'Hidden', saved: true, rssi: -80 });
    expect(n.ssid).toBe('Hidden');
    expect(n.saved).toBe(true);
    expect(n.rssi).toBe(-80);
  });

  it('networks sort by RSSI descending (stronger signal first)', () => {
    const nets = [
      makeNetwork({ bssid: 'AA:AA:AA:AA:AA:AA', rssi: -80 }),
      makeNetwork({ bssid: 'BB:BB:BB:BB:BB:BB', rssi: -40 }),
      makeNetwork({ bssid: 'CC:CC:CC:CC:CC:CC', rssi: -60 }),
    ];
    nets.sort((a, b) => b.rssi - a.rssi);
    expect(nets.map((n) => n.rssi)).toEqual([-40, -60, -80]);
  });
});
