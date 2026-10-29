---
kind: fixed
---
Write default-valued config fields so downgrades keep OTA updates enabled

## Release Note
Downgrading no longer disables firmware updates

Configs saved by 1.6.0 left out settings that matched their default. Older firmware uses different defaults, so after downgrading to 1.5.x the hub had OTA updates, backend-managed updates and keep-alive switched off, and ignored update requests from the website. Every setting is now written explicitly.
