import type { WifiScanStatus } from '$lib/_fbs/open-shock/serialization/types/wifi-scan-status';
import type { WiFiNetwork, WiFiNetworkGroup } from './';

export type DeviceState = {
  /// The BSSID of the currently connected WiFi network, or null if not connected.
  wifiConnectedBSSID: string | null;

  /// The status of the WiFi scan, or null if not scanning.
  wifiScanStatus: WifiScanStatus | null;

  /// A map of BSSID -> WiFiNetwork for all networks thought to still be in range.
  wifiNetworks: Map<string, WiFiNetwork>;

  /// A map of SSID + AuthMode -> WiFiNetworkGroup for all networks thought to still be in range.
  wifiNetworkGroups: Map<string, WiFiNetworkGroup>;

  /// The SSIDs of all saved networks.
  wifiSavedNetworks: string[];

  /// The IP address that the device has been assigned by the WiFi network, or null if not yet assigned.
  wifiIpAddress: string | null;

  /// Whether the device is paired with the gateway.
  accountLinked: boolean;

  /// The RF TX pin, or null if not set.
  rfTxPin: number | null;
};
