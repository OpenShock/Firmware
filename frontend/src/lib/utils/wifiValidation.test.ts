import { describe, expect, it } from 'vitest';
import {
  accessPointPassphraseError,
  accessPointSsidError,
  isValidWifiPassword,
  isValidWifiSsid,
} from './wifiValidation';

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

describe('accessPointSsidError', () => {
  it('accepts the default (empty) and names up to 32 bytes', () => {
    expect(accessPointSsidError('')).toBeNull();
    expect(accessPointSsidError('My Hub')).toBeNull();
    expect(accessPointSsidError('é'.repeat(16))).toBeNull();
  });

  it('rejects names over 32 bytes', () => {
    expect(accessPointSsidError('é'.repeat(17))).toMatch(/32 bytes/);
  });

  it("rejects '|' and control characters", () => {
    expect(accessPointSsidError('My|Hub')).not.toBeNull();
    expect(accessPointSsidError('My\tHub')).not.toBeNull();
    expect(accessPointSsidError('My\x7fHub')).not.toBeNull();
  });
});

describe('accessPointPassphraseError', () => {
  it('accepts 8 to 63 printable ASCII characters', () => {
    expect(accessPointPassphraseError('12345678')).toBeNull();
    expect(accessPointPassphraseError('~'.repeat(63))).toBeNull();
  });

  it('rejects short and long passphrases', () => {
    expect(accessPointPassphraseError('')).toMatch(/at least 8/i);
    expect(accessPointPassphraseError('a'.repeat(64))).toMatch(/at most 63/i);
  });

  it('rejects anything but printable ASCII', () => {
    expect(accessPointPassphraseError('pass\tword')).not.toBeNull();
    expect(accessPointPassphraseError('café-latte')).not.toBeNull();
  });
});
