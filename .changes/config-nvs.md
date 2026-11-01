---
kind: changed
pr: 548
---
Keep the configuration in NVS instead of a littlefs file

## Release Note
Settings are now stored in NVS

The hub keeps its settings in the chip's NVS key-value store instead of a single config file, and saves each setting on its own when it changes. Hubs on 1.x get here through the migration firmware, which carries your WiFi networks, account link and other settings over.

- Each module owns its section of the config (`config/Section.h`); every section type meets the `SectionValue` concept, with its defaults defined in one place and its validation run on every value the section takes in
- The store is the `cfg` partition; an empty one reads as the defaults, and a setting is only written once it changes
- WiFi credentials are stored one per key; the WiFi driver's own NVS store is compiled out
- A section that can't be read runs on its defaults without erasing what is stored, and the save after a failed one rewrites every key; a single WiFi credential that doesn't read is dropped on its own, so the rest still load
- A `cfg` partition in a newer NVS format is never erased
- A section whose NVS namespace fails to open is a failed load, not a load of the defaults: the defaults are only taken when the namespace was never written
- A boolean field of the wrong type in a `jsonconfig set` document rejects the document instead of storing the default, in every section
- The config accessors return the value itself (a section always has one); the WiFi section hands a network saved without an id the lowest free one on every path, so `networks` over serial no longer allocates ids itself
- An OTA check interval of zero and a hostname esp_netif would refuse become their defaults on every path (NVS, JSON and the setters), not only through the captive portal

## Notices
- important: **Hubs on 1.x must install the migration firmware first.** This release only runs on the new partition table, so a 1.x hub cannot update to it directly. Install the migration firmware, then update as usual.
