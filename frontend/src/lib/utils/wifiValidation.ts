// Mirrors the hub's checks (CaptivePortalInstance apiWifiNetworksAdd): lengths are in bytes, not UTF-16 units.
const encoder = new TextEncoder();

function byteLength(value: string): number {
  return encoder.encode(value).length;
}

/** 802.11 SSIDs are 1-32 bytes. */
export function isValidWifiSsid(ssid: string): boolean {
  const length = byteLength(ssid);
  return length >= 1 && length <= 32;
}

/** WPA passphrases are 8-63 characters. */
export function isValidWifiPassword(password: string): boolean {
  const length = byteLength(password);
  return length >= 8 && length <= 63;
}
