---
'firmware': patch
---

refactor: Split OTA update system into Manager, Client, and FirmwareCDN modules

- **OtaUpdateManager**: Watcher task with WiFi event handling, periodic update checks, and firmware lifecycle management (boot type, validation, rollback)
- **OtaUpdateClient**: Single-shot update execution — fetches release metadata, flashes filesystem and app partitions, reboots into new firmware
- **FirmwareCDN**: HTTP client for the firmware CDN — version checks, board listings, binary hash fetching, and release info assembly
