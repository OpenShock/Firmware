---
kind: safety
---
Refuse disabling or re-pinning the E-Stop while it is active

## Release Note
An active E-Stop can no longer be cleared by changing its settings

Disabling the E-Stop or moving it to another pin restarted its monitoring and cleared an active E-Stop without the button being released. Both are now refused while the E-Stop is active, from the captive portal and from serial.
