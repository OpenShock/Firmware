---
kind: security
---
Remove the captive portal endpoint for changing the OTA domain

## Release Note
The OTA update domain can no longer be changed from the captive portal

The setup access point is open and unauthenticated, and firmware updates trust the hashes served by the OTA domain. The domain is now read-only in the captive portal and can only be changed over serial.
