import type { WifiScanStatus } from '$lib/_fbs/open-shock';
import type { WiFiNetwork } from './WiFiNetwork';

export type WiFiState = {
  initialized: boolean;
  connected: string | null;
  scan_status: WifiScanStatus | null;
  networks: Map<string, WiFiNetwork>;
};
