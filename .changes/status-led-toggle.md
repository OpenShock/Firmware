---
kind: added
---
Turn the status LEDs off

## Release Note
The status LEDs can now be turned off with the serial command `statusled false`, for hubs in a bedroom

With the status LEDs off, the hub stays dark during normal operation and only lights up for an active E-Stop or a critical error. `statusled true` turns them back on.

- Add the `led` config section (NVS namespace `led`, key `status` (u8); `led.statusEnabled` in jsonconfig), defaulting to on
- The LED test (`ledtest`) still shows every pattern while the status LEDs are off
