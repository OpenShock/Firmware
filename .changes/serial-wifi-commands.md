---
kind: added
---
Add WiFi serial commands

## Release Note
The serial console can now show and manage the WiFi connection: `wifi status`, `wifi scan`, `wifi list`, `wifi connect`, `wifi disconnect` and `wifi forget`

- `wifi` / `wifi status`: connection state as JSON (SSID, BSSID, channel, RSSI, auth mode, IP) and whether a scan is running
- `wifi scan` starts a scan, `wifi list` prints the networks found as JSON
- `wifi connect <ssid> [password]` connects to a saved network, or saves it first when a password is given; an SSID with spaces goes in double quotes
- `wifi disconnect` drops the current connection; the hub reconnects to a saved network on its own
- `wifi forget <ssid>` removes a saved network and disconnects from it if it is the current one
