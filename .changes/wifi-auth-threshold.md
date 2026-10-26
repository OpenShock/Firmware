---
kind: fixed
---
Let the WiFi driver enforce the saved network's security, so WPA and WEP networks can connect

## Release Note
Older WPA and WEP networks can now be connected to

- Saved networks using WPA or WEP security could never connect, because the WiFi driver required WPA2 by default
- The hub now refuses any access point whose security is weaker than the network had when it was saved, including ones that were not in the last scan, and the portal says why
