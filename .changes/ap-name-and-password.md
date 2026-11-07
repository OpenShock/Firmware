---
kind: added
---
Set the captive portal's network name and password, protected by default

## Release Note
The captive portal's WiFi network can now have its own name and a WPA2/WPA3 password, set in the captive portal's new Access Point section or over serial with `ap ssid` and `ap password`

The portal's network is now protected by default with the password `openshock`, including on hubs that update from an older version. It is public, so it only keeps out passers-by. Phones and computers that support WPA3 get a connection that someone listening nearby can't decrypt even with the password, but anyone who knows it can set up a fake network with the same name. Set your own password or open the network with `ap open`. A factory reset restores the default password, so a forgotten one can always be recovered over USB.

- Use the stored `wifi.accessPointSSID` for the access point; it was saved but never applied. The default name still gets the hub's MAC appended
- Add `wifi.accessPointPassword` (NVS key `ap_pass` in `wifi.cfg`, exported in jsonconfig only with sensitive data): defaulting to the new Kconfig option `OPENSHOCK_FW_AP_PASSWORD` (`openshock`, checked at compile time), empty for an open network, else 8-63 printable ASCII characters, with WPA2/WPA3-PSK transition mode (WPA3 clients use SAE, WPA2 clients still connect)
- Add the `ap` serial command: `ap` shows the name and whether it is protected, `ap ssid <name|default>`, `ap password <password|default>` and `ap open`
- Add `PUT /api/ap` to the captive portal API (form body: `ssid`, empty for the default; `protection` `default`, `open` or `custom` with `password`; answers with the saved `ssid` and `protection`), and the access point's name and protection, never its password, to the portal's Ready message
- Changes apply the next time the captive portal starts
