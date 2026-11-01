---
kind: changed
---
Move hubs from the 1.x firmware CDN domain to the repository server for OTA updates

## Release Note
Hubs updated from 1.x now look for updates on the repository server

The 1.x default `firmware.openshock.org` serves flat files for 1.x hubs and has no repository API, so a hub that kept it could not update again. A hub storing that domain now uses `repo.openshock.dev`, which is also the new default. A custom domain is left as it is.
