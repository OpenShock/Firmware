---
kind: fixed
pr: 536
---
Build without a board fragment with the 4 MB OTA partition table

## Release Note
A build without a board fragment (the VS Code ESP-IDF extension, a plain `idf.py build`) fell back to ESP-IDF's stock 2 MB flash and 1 MB single-app partition table. The app overflowed it, and the staticfs target then tried to size an image from a partition the table did not have, because its guard compared an undefined CMake variable name to the empty string. `sdkconfig.defaults` now carries the 4 MB flash and the `partitions/ota_4mb.csv` table every board already uses, and a table without that partition now stops the build with a clear error.
