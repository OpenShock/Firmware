---
kind: added
---
Download gzip-compressed app images during OTA updates

## Release Note
Firmware updates download about a third less when the release has a gzip copy of the app binary, which helps most on slow WiFi. Releases without one update as before.

- Tries the app binary URL + `.gz` first (gzip with a 4 KB window, `wbits` 12, decompressed with espressif/zlib) and falls back to the plain image on any failure; `OPENSHOCK_OTA_GZIP` turns it off
