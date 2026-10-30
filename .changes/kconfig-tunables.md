---
kind: changed
---
Move build-time settings into Kconfig

## Release Note
Build-time settings can now be changed in menuconfig

- Task stacks and priorities, timeouts, retry intervals, queue and buffer sizes are now set in `menuconfig`, in the OpenShock menus under Component config
- The default API and firmware CDN domains, hostname, SoftAP SSID prefix, captive portal IP address and venue info URL are set there too
- The log level can be overridden in `menuconfig`; by default it still follows the build mode
