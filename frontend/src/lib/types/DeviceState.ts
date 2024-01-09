import type { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import type { Config } from '$lib/mappers/ConfigMapper';
import type { WiFiNetwork, WiFiNetworkGroup } from './';

export type DeviceState = {
  wifiConnectedBSSID: string | null;
  wifiScanStatus: WifiScanStatus | null;
  wifiNetworks: Map<string, WiFiNetwork>;
  wifiNetworkGroups: Map<string, WiFiNetworkGroup>;
  accountLinked: boolean;
  config: Config | null;
};
