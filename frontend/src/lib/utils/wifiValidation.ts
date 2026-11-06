// Mirrors the hub's checks (the portal's WiFi and access point handlers, wifi/WiFiConfig.h): lengths are in bytes,
// not UTF-16 units.
const encoder = new TextEncoder();

export const SSID_MAX_BYTES = 32; // 802.11
export const PASSPHRASE_MIN_LENGTH = 8; // WPA2/WPA3
export const PASSPHRASE_MAX_LENGTH = 63;

function byteLength(value: string): number {
  return encoder.encode(value).length;
}

/** 802.11 SSIDs are 1-32 bytes. */
export function isValidWifiSsid(ssid: string): boolean {
  const length = byteLength(ssid);
  return length >= 1 && length <= SSID_MAX_BYTES;
}

/** WPA passphrases are 8-63 characters. */
export function isValidWifiPassword(password: string): boolean {
  const length = byteLength(password);
  return length >= PASSPHRASE_MIN_LENGTH && length <= PASSPHRASE_MAX_LENGTH;
}

/**
 * Why the hub would refuse `ssid` as its access point's name, or null. Empty is fine: it means the default name.
 * The hub also refuses '|' and control characters, since its serial console prints the name as a field.
 */
export function accessPointSsidError(ssid: string): string | null {
  if (byteLength(ssid) > SSID_MAX_BYTES) return `At most ${SSID_MAX_BYTES} bytes`;
  if (/[|\x00-\x1f\x7f]/.test(ssid)) return "No '|' or control characters";
  return null;
}

/** Why the hub would refuse `passphrase` for its access point, or null: it takes 8-63 printable ASCII characters. */
export function accessPointPassphraseError(passphrase: string): string | null {
  const length = byteLength(passphrase);
  if (length < PASSPHRASE_MIN_LENGTH) return `At least ${PASSPHRASE_MIN_LENGTH} characters`;
  if (length > PASSPHRASE_MAX_LENGTH) return `At most ${PASSPHRASE_MAX_LENGTH} characters`;
  if (!/^[\x20-\x7e]*$/.test(passphrase)) return 'Only plain letters, digits, spaces and symbols';
  return null;
}
