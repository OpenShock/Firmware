import { describe, expect, it } from 'vitest';
import { isValidWifiPassword, isValidWifiSsid } from './wifiValidation';

describe('isValidWifiSsid', () => {
  it('accepts 1 to 32 bytes', () => {
    expect(isValidWifiSsid('a')).toBe(true);
    expect(isValidWifiSsid('a'.repeat(32))).toBe(true);
  });

  it('rejects empty and over-long SSIDs', () => {
    expect(isValidWifiSsid('')).toBe(false);
    expect(isValidWifiSsid('a'.repeat(33))).toBe(false);
  });

  it('counts UTF-8 bytes, not characters', () => {
    // 'é' is 2 bytes: 16 of them are 32 bytes, 17 are 34
    expect(isValidWifiSsid('é'.repeat(16))).toBe(true);
    expect(isValidWifiSsid('é'.repeat(17))).toBe(false);
  });
});

describe('isValidWifiPassword', () => {
  it('accepts 8 to 63 characters', () => {
    expect(isValidWifiPassword('12345678')).toBe(true);
    expect(isValidWifiPassword('a'.repeat(63))).toBe(true);
  });

  it('rejects short and over-long passwords', () => {
    expect(isValidWifiPassword('1234567')).toBe(false);
    expect(isValidWifiPassword('a'.repeat(64))).toBe(false);
  });
});
