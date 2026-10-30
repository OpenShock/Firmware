---
kind: changed
---
Keep the configuration in NVS instead of a littlefs file

## Release Note
Settings are now stored in NVS

The hub keeps its settings in the chip's NVS key-value store instead of a single config file, and saves each setting on its own when it changes. Your WiFi networks, account link and other settings from 1.x are imported automatically on the first boot after updating.

- Each module owns its section of the config (`config/Section.h`); every section type meets the `SectionValue` concept, with its defaults defined in one place and its validation run on every value the section takes in
- On the 2.x partition table the store is the `cfg` partition (the 1.x littlefs `config` partition retyped in place); on the 1.x table it lives in the shared `nvs` partition until the layout is migrated, and then moves into `cfg`
- An empty store is filled once from the 1.x config file, which is never modified, otherwise from the defaults
- WiFi credentials are stored one per key; the WiFi driver's own NVS store is compiled out
- A section that can't be read runs on its defaults without erasing what is stored, and the save after a failed one rewrites every key
- A store written by newer firmware is loaded instead of reset, and a `cfg` partition in a newer NVS format is never erased
- `Config::MigrateToCfgPartition` retypes the `config` partition as `cfg`; nothing calls it yet

## Notices
- warning: **1.x firmware does not see settings changed after this update.** Downgrading to 1.x brings back the settings the hub had before updating. Re-enter any WiFi networks or pairing you changed since.
