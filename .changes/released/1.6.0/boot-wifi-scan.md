---
kind: fixed
---
Stop the boot WiFi scan colliding with the cached network reconnect

## Release Note
No more WiFi scan error at startup

At boot the hub started a WiFi scan while it was already reconnecting to the saved network, which failed and logged "Failed to start scan on channel 0". The scan now waits for the reconnect, and scan errors report the actual channel.
