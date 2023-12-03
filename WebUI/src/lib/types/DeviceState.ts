import type { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import type { WiFiNetwork } from './WiFiNetwork';

export type DeviceState = {
  wsConnected: boolean;
  deviceIPAddress: string | null;
  wifiScanStatus: WifiScanStatus | null;
  wifiNetworksSaved: string[];
  wifiNetworksPresent: Map<string, WiFiNetwork>;
  wifiConnectedBSSID: string | null;
  accountLinked: boolean;
  rfTxPin: number | null;
};
