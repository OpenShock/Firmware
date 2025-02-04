import type { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';

export type WiFiNetworkGroup = {
  ssid: string;
  saved: boolean;
  security: WifiAuthMode;
  networks: {
    bssid: string;
    rssi: number;
    channel: number;
  }[];
};
