import type { WifiAuthMode } from '$lib/_fbs/open-shock/serialization/types/wifi-auth-mode';
import type { WiFiNetwork } from './WiFiNetwork';

export type WiFiNetworkGroup = {
  ssid: string;
  saved: boolean;
  security: WifiAuthMode;
  networks: WiFiNetwork[];
};
