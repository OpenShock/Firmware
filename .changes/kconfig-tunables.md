---
kind: changed
---
Move build-time settings into Kconfig

## Release Note
Build-time settings can now be changed in menuconfig

- Task stacks and priorities, timeouts, retry intervals, queue and buffer sizes are now set in `menuconfig` under OpenShock, each next to the component that uses it
- The log level can be overridden in `menuconfig`; by default it still follows the build mode
