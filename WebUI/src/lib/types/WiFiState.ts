import type { WiFiNetwork } from './WiFiNetwork';

export type WiFiState = {
  initialized: boolean;
  scanning: boolean;
  networks: { [bssid: string]: WiFiNetwork };
};
