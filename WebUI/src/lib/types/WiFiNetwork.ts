export type WiFiNetwork = {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  security: 'Open' | 'WEP' | 'WPA PSK' | 'WPA2 PSK' | 'WPA/WPA2 PSK' | 'WPA2 Enterprise' | 'WPA3 PSK' | 'WPA2/WPA3 PSK' | 'WAPI PSK' | null;
  saved: boolean;
};
