---
kind: changed
---
Read the board's pins and OTA identity from a descriptor stamped into the app image instead of compile-time macros

## Release Note
Board identity now lives in the firmware image itself

Each firmware image carries a 256-byte board descriptor (board name, chip, pins, LED flags, pin bypasses) at a fixed offset, written by `scripts/stamp_board.py` after linking (the build does this for the configured board). The compiled image itself describes no board and has no pins, so an image that was never stamped boots into the captive portal instead of driving pins it assumed. The firmware reads its pins and the board name it asks the repository server for from that descriptor, so one compile per chip can serve every board on it. The descriptor travels with the app through OTA, so it survives formatting the config, NVS and static partitions, and a rollback restores the matching one. The OTA updater now refuses a downloaded image whose descriptor names a different board before making it bootable, and an image that nonetheless boots on the wrong board rolls back on its first boot. Board fragments gain an `OPENSHOCK_GPIO_BYPASS` line, replacing the per-board bypass lists that were compiled into `Chipset.h`.
