---
kind: changed
pr: 552
---
Move hubs from the 1.x firmware CDN domains to the repository server for OTA updates

## Release Note
Hubs that stored a 1.x update domain now look for updates on the repository server

The 1.x domains `firmware.openshock.org` and `fw.openshock.org` serve flat files for 1.x hubs and have no repository API, so a hub that kept one could not update again. A hub storing either now uses `repo.openshock.dev`, which is also the new default. A custom domain is left as it is.
