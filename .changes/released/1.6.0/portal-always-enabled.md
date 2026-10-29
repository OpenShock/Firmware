---
kind: fixed
---
Keep the always-enabled captive portal setting across reboots and setup completion

## Release Note
The captive portal now stays enabled when you turn it on

Enabling the captive portal from the website was lost on the next reboot, so it had to be turned on again. Finishing the setup wizard also briefly restarted the setup access point, disconnecting every device connected to it. The setting is now restored at boot and the portal stays open while it is enabled.
