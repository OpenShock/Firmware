---
kind: added
---
Set the captive portal's network name and password

## Release Note
The captive portal's WiFi network can now have its own name and a WPA2 password, set over serial with `ap ssid` and `ap password`

By default the portal's network stays open and named after the hub, as before. A factory reset removes the password, so a forgotten one can always be recovered over USB.

- Use the stored `wifi.accessPointSSID` for the access point; it was saved but never applied. The default name still gets the hub's MAC appended
- Add `wifi.accessPointPassword` (NVS key `ap_pass` in `wifi.cfg`, exported in jsonconfig only with sensitive data): empty for an open network, else 8-63 characters, with WPA2-PSK
- Add the `ap` serial command: `ap` shows the name and whether it is protected, `ap ssid <name|default>`, `ap password <password>` and `ap open`
- Changes apply the next time the captive portal starts
