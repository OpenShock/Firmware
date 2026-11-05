---
kind: added
---
Set the captive portal's network name and password, protected by default

## Release Note
The captive portal's WiFi network can now have its own name and a WPA2/WPA3 password, set over serial with `ap ssid` and `ap password`

The portal's network is now protected by default with the password `openshock`, including on hubs that update from an older version. It is public, but phones and computers that support WPA3 still get an encrypted connection nobody nearby can read. Set your own password or open the network with `ap open`. A factory reset restores the default password, so a forgotten one can always be recovered over USB.

- Use the stored `wifi.accessPointSSID` for the access point; it was saved but never applied. The default name still gets the hub's MAC appended
- Add `wifi.accessPointPassword` (NVS key `ap_pass` in `wifi.cfg`, exported in jsonconfig only with sensitive data): defaulting to the new Kconfig option `OPENSHOCK_FW_AP_PASSWORD` (`openshock`, checked at compile time), empty for an open network, else 8-63 characters, with WPA2/WPA3-PSK transition mode (WPA3 clients use SAE, WPA2 clients still connect)
- Add the `ap` serial command: `ap` shows the name and whether it is protected, `ap ssid <name|default>`, `ap password <password|default>` and `ap open`
- Changes apply the next time the captive portal starts
