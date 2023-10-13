import type { WifiAuthMode } from '$lib/fbs/open-shock/wifi-auth-mode';

export type WiFiNetwork = {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  security: WifiAuthMode;
  saved: boolean;
};
