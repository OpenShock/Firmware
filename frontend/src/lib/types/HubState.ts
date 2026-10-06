import type { WifiScanStatus } from '#lib/_fbs/open-shock/serialization/types/wifi-scan-status.js';
import type { Config } from '#lib/mappers/ConfigMapper.js';
import type { WiFiNetwork, WiFiNetworkGroup } from './';

export type HubState = {
  wifiConnectedBSSID: string | null;
  wifiScanStatus: WifiScanStatus | null;
  wifiNetworks: Map<string, WiFiNetwork>;
  wifiNetworkGroups: Map<string, WiFiNetworkGroup>;
  accountLinked: boolean;
  config: Config | null;
  gpioValidInputs: Int8Array;
  gpioValidOutputs: Int8Array;
};
