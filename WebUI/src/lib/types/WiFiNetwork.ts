export type WiFiNetwork = {
  index: number;
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  secure: boolean;
  saved: boolean;
};
