# Storage and partition layouts

How the firmware lays out flash, where it keeps its config and the captive portal's assets, and how a 1.x hub's
littlefs `config` and `static0` partitions become the NVS config store and the `www` image partition in place. The
reasoning behind each choice is in the design document (OpenShock Firmware 2.0: Storage and Partition Migration
Design); this page describes what the code does.

## Partition tables

One table, `partitions/ota_4mb.csv`, for every board whatever its flash size. Board files never name it.

| Partition | Offset | Size | 1.x (`partitions/legacy/ota_4mb_v1.csv`) |
| --- | --- | --- | --- |
| `nvs` | 0x9000 | 20 KiB | the same |
| `otadata` | 0xE000 | 8 KiB | the same |
| `app0`, `app1` | 0x10000, 0x1B0000 | 1,664 KiB each | the same |
| `cfg` (data/nvs) | 0x350000 | 12 KiB | littlefs `config` (data/spiffs) |
| `www` (type 0x40) | 0x353000 | 628 KiB | littlefs `static0` (data/spiffs) |
| `coredump` | 0x3F0000 | 64 KiB | the same |

It is the 1.x geometry entry for entry: a fielded hub rewrites its table in place and nothing moves. Only `config` and
`static0` change, in type and label, each by its own migration; either can run first. The flash past 4 MB on bigger
boards stays unused.

## Config

The config lives in the `cfg` NVS partition as typed keys, one namespace per section. At 12 KiB (three NVS pages, one
always kept free for compaction) it holds the whole config with some 32 long WiFi credentials, more than the 4 KiB
1.x config file did; it fills up at about 36. The config component
(`components/config`) is only the framework; each module owns its section, its keys and its accessors:

| Section | Owner | NVS namespaces |
| --- | --- | --- |
| `rf` | device_control (`radio/RFConfig.h`) | `rf` |
| `wifi` | core (`wifi/WiFiConfig.h`) | `wifi.cfg`, `wifi` (one blob per credential, key = its id) |
| `captivePortal` | captive_portal (`captiveportal/CaptivePortalConfig.h`) | `portal` |
| `backend` | core (`BackendConfig.h`) | `backend` |
| `serialInput` | serial_console (`serial_console/SerialInputConfig.h`) | `serial` |
| `otaUpdate` | core (`OtaUpdateConfig.h`) | `ota` |
| `estop` | device_control (`estop/EStopConfig.h`) | `estop` |

The store's own namespace is `root`, with `schema` (u8, 1) marking a filled store. Namespace and key names are at
most 15 characters and are permanent once hubs hold them: renaming one is a migration, not an edit. A missing key
reads as its default and an unknown key is ignored, so adding or removing a field never changes the schema.

To add a section, define a value type whose default constructor is the defaults, with `Normalize` (its validation,
run on every value the section takes in), `LoadNvs`, `SaveNvs`, `FromJSON`, `ToJSON` and `ToFlatbuffers`, and one
`Config::Section<T>` for it in the module (the `SectionValue` concept in `config/Section.h` is the contract). It
joins `jsonconfig`, `rawconfig`, the local protocol's Ready message and factory reset through the section registry.
A load opens its namespace with `Nvs::Handle::OpenForLoad`: a namespace never written reads every key as its
default, an open that failed fails the load, and a failed load runs the section on its defaults without trusting
flash. A save goes through `Nvs::Saver`, which writes only the members that differ from the previous value.

Where the store is:

- `cfg` once the table has it. On the first boot after the table rewrite `cfg` still holds the 1.x littlefs; it is
  erased before NVS opens it.
- On the 1.x table, which has no `cfg`, the same namespaces in the shared `nvs` partition. The first boot that finds
  `cfg` copies them over key by key, compares the copy, and deletes the shared one.
- An empty store is filled once: from the 1.x littlefs config file (`/config` on the 1.x `config` partition, read
  only, so a rollback to 1.x still finds it until the config partition migration below has run) when there is one,
  else from every section's defaults. The store is marked filled only once every section reads back from flash as
  written; a fill that failed part-way is redone on the next boot.
- Without any usable NVS, the hub runs on defaults in RAM and logs it.

The shared `nvs` belongs to ESP-IDF (PHY calibration cache) and may be erased at any time; nothing of OpenShock's is
kept there once `cfg` exists. Until then an erased shared `nvs` is an empty store: the hub imports the 1.x file again
and loses what changed since the update. The WiFi driver's own NVS store is compiled out (`CONFIG_ESP_WIFI_NVS_ENABLED=n`).
`sysinfo` on the serial console prints where the config is.

## The config partition migration

`Config::MigrateToCfgPartition()` (`config/Migration.h`) moves a 1.x hub onto `cfg`. Nothing calls it yet: the release
that pipelines the storage migrations decides when it runs.

1. It refuses unless the live table has the 1.x littlefs `config` partition and no `cfg`, and the config is in the
   shared `nvs` (where a 1.x hub keeps it after its first boot on this firmware, imported from the 1.x file).
2. It retypes `config` as `cfg` (data/nvs) in a copy of the live table and re-seals the MD5
   (`components/partition_layout`). Every offset and size stays, so the bootloader that boots the 1.x table boots it.
3. Config writes are held, the WiFi driver's stale `nvs.net80211` store is dropped, NVS is closed, and the table sector
   is erased and written through the OTA path and read back. A write that will not stick puts the old table back; the
   hub never restarts without a table that parses.
4. The hub restarts; `Store` erases the littlefs left in `cfg` (the 1.x config file with it: from here a downgrade to
   1.x starts from defaults) and moves the config in.

## Portal assets

The captive portal's assets are one flat read-only image in the `www` partition (type 0x40, so no ESP-IDF component
tries to mount it), mapped once and served by pointer. `scripts/utils/wwwimage.py` writes it and
`components/www/include/www/WwwImage.h` documents the format: a directory tree with sorted children, 4-byte aligned
gzipped bodies and a SHA-256 over the whole image (header included, its own field zeroed), checked before anything is
served.
The same image serves every board. The OTA update skips flashing it when the hash on flash already matches the
release's.

Releases from 2.0.0 publish the image as `www.bin` under the repository server's `staticfs` artifact. A 1.x-numbered
release (the 1.9.x pivot that every fielded hub installs first) publishes the littlefs `staticfs.bin` there instead,
packed from the same assets by `gen_staticfs.py --legacy-output`: a 1.x hub installs an update with its own OTA code,
which writes `staticfs` over its littlefs `static0` and aborts unless that then mounts. `scripts/repo_server_publish.py`
picks the file by the release's major version.

On the 1.x table, which has no `www`, this firmware serves the portal from the littlefs `static0` and its OTA update
flashes `static0` and verifies it by mounting, as the 1.x firmware does. Nothing on a 1.x hub touches the partition
table until the www migration below runs.

## The www partition migration

`Www::MigrateToWwwPartition()` (`www/Migration.h`) moves a 1.x hub onto `www`. Nothing calls it yet: the release that
pipelines the storage migrations decides when it runs, with the portal closed and no OTA install running.

1. It refuses unless the live table has the 1.x littlefs `static0` partition and no `www`.
2. It retypes `static0` as `www` (type 0x40) in a copy of the live table and re-seals the MD5
   (`components/partition_layout`). Every offset and size stays, so the bootloader that boots the 1.x table boots it.
3. It downloads the release's www image over `static0`, checks its SHA-256 against the published one, and checks that
   what is on flash parses and hashes the same. From here the 1.x assets are gone; a failure leaves the portal without
   assets until a retry succeeds.
4. The table sector is erased and written through the OTA path and read back. A write that will not stick puts the
   old table back; the hub never restarts without a table that parses.
5. The hub restarts and the portal serves from `www`.

## Recovery

The table sector's erase-to-write window (one sector, well under a second, once per hub) is the one step a power cut
cannot recover from: the bootloader then finds no partition table. The app, config and assets are still on flash, so
writing the board's table restores the hub with its config:

```sh
esptool write_flash 0x8000 partition-table.bin    # this board's, from its release
```

Flashing the merged image for the board at 0x0 also works but erases the config.
