import { afterEach, describe, expect, it, vi } from 'vitest';
import { getApiBaseUrl, getDeviceHostname } from './localRedirect';

function setHostname(hostname: string) {
  vi.stubGlobal('window', { location: { hostname } });
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('getDeviceHostname', () => {
  it('maps localhost and 127.0.0.1 to the device host', () => {
    setHostname('localhost');
    expect(getDeviceHostname()).toBe('4.3.2.1');
    setHostname('127.0.0.1');
    expect(getDeviceHostname()).toBe('4.3.2.1');
  });

  it('passes the device and other hostnames through unchanged', () => {
    setHostname('4.3.2.1');
    expect(getDeviceHostname()).toBe('4.3.2.1');
    setHostname('openshock.local');
    expect(getDeviceHostname()).toBe('openshock.local');
  });
});

describe('getApiBaseUrl', () => {
  it('uses an absolute device URL only when served locally', () => {
    setHostname('localhost');
    expect(getApiBaseUrl()).toBe('http://4.3.2.1');
    setHostname('4.3.2.1');
    expect(getApiBaseUrl()).toBe('');
  });
});
