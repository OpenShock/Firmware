import type { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import type { WiFiNetwork } from './WiFiNetwork';

export type WiFiState = {
  wifiConnectedBSSID: string | null;
  wifiScanStatus: WifiScanStatus | null;
  wifiNetworks: Map<string, WiFiNetwork>;
  gatewayPaired: boolean;
  rfTxPin: number | null;
};
