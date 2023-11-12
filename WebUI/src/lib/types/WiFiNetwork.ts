import type { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';

export type WiFiNetwork = {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  security: WifiAuthMode;
  saved: boolean;
};
