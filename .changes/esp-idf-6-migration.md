---
kind: changed
breaking: true
---
Rebuild the firmware on the native ESP-IDF platform

## Release Note
This is one of the biggest updates the firmware has ever had: the whole thing has been rebuilt from the ground up on Espressif's native platform, leaving the old Arduino foundation behind.

You won't see a pile of new buttons from this release — most of the work is under the hood. What you get is a firmware that's faster to start up, lighter on memory, and far more stable, with a much cleaner codebase that makes future features quicker and safer to add. It also brings a real security upgrade: your hub now checks that it's genuinely talking to OpenShock's servers before it trusts them, so the connection between your device and the network is properly protected.

Just about every part of the firmware was touched along the way — WiFi and networking, the way settings are stored, the radio that talks to your shockers, the status LEDs, and the serial console. Everything has been retested and put back together on the new foundation.

Because this rework runs so deep, we're treating it as a major release. It flashes and runs like any other update, and your existing setup carries over.
