---
kind: fixed
---
Build without a board fragment with the 4 MB OTA partition table, and skip staticfs cleanly without one

## Release Note
A build without a board fragment (the VS Code ESP-IDF extension, a plain `idf.py build`) fell back to ESP-IDF's stock 2 MB flash and 1 MB single-app partition table. The app overflowed it, and the staticfs target then tried to size an image from a `static0` partition the table did not have, because its guard compared an undefined CMake variable name to the empty string. `sdkconfig.defaults` now carries the 4 MB flash and the `partitions/ota_4mb.csv` table every board already uses, and a table without `static0` skips the staticfs target with the intended warning.
