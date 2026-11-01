---
kind: changed
---
Run the 1.x -> 2.x storage migration on hubs still on the 1.x partition table

## Release Note
Hubs updated from 1.x move to the new storage layout on their own

On the first boot after updating, a hub still on the 1.x partition table turns its `config` partition into the NVS config store and restarts; once online it downloads this release's portal assets over `static0`, turns it into the `www` partition and restarts again. Nothing moves on flash and the settings carry over. Do not unplug the hub while it says so on the serial console.

- `components/migration` pipelines `Config::MigrateToCfgPartition` and `Www::MigrateToWwwPartition` in one task started from `app_main` after setup; on the 2.x table it starts nothing
- The OTA manager has one flash-write lock: an install holds it from its start to its restart, a migration step for its whole run, and whichever comes second backs off
- `migrate` over serial reports whether a migration is pending, `migrate now` retries at once, and `migrate www <url> <sha256>` runs the www step from an image of your own, for builds with no published release

## Notices
- warning: **Downgrading to 1.x is no longer possible once the migration has run.** The 1.x config file and portal assets are gone from flash; a downgrade starts from factory defaults.
