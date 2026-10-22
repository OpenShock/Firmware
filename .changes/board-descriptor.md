---
kind: changed
---
Stamp a board descriptor into the firmware image instead of compiling each board in

## Release Note
Firmware images now carry a small board descriptor: which board they are for, which chip, and the pins and LED settings that board uses. The firmware reads it at startup instead of having those values baked in at compile time, so one build per chip can serve every board on that chip, and adding a board no longer means adding a build. Nothing changes for you when updating: the hub asks for updates by the same board name as before, and the descriptor travels inside the app image, so it survives resetting the configuration and follows a rollback.

- Add a 256-byte board descriptor at a fixed offset of the app image, written by `scripts/stamp_board.py` after linking and validated on the device with a CRC
- Read pins, LED flags, pin bypasses and the OTA board name from the descriptor at startup; the compiled image describes no board and has no pins until it is stamped
- Refuse an OTA download whose descriptor names a different board before it is made bootable, and roll back on first boot if the previous slot disagrees
- Boot into the captive portal with no pins, and say so on the serial console, when an image was never stamped or its descriptor is corrupt
- Validate the descriptor at boot: chip matches the build, every pin is usable on the chip, no two features share a pin; an unusable LED pin disables that LED instead of failing startup
- Move per-board pin bypass lists out of `Chipset.h` into an `OPENSHOCK_GPIO_BYPASS` line in each board fragment
- Stamp the configured board automatically in local builds, and verify in CI that every built image carries the right descriptor and that re-stamping it is byte-identical
