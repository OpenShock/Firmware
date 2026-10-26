# Storage and partition layouts

How the firmware lays out flash, where it keeps its config and portal assets, and how a 1.x hub moves to the 2.x
layout over the air. The reasoning behind each choice is in the design document (OpenShock Firmware 2.0: Storage and
Partition Migration Design); this page describes what the code does.

## Partition tables

One table per flash size, picked from the board's `CONFIG_ESPTOOLPY_FLASHSIZE_*` by `scripts/utils/partitions.py`
(and by the root `CMakeLists.txt` for preset builds). Board files never name a table.

| Table | Boards | app0 / app1 | cfg | www |
| --- | --- | --- | --- | --- |
| `partitions/ota_4mb.csv` | every 4 MB board | 1,824 KiB each | 32 KiB at 0x1D8000 | 288 KiB at 0x3A8000 |
| `partitions/ota_8mb.csv` | 8 MB and 16 MB boards | 3,680 KiB each | 32 KiB at 0x3A8000 | 256 KiB at 0x3C0000 |

`nvs` (0x9000), `otadata` (0xE000) and the start of `app0` (0x10000) are the same in every table, the 1.x one
(`partitions/legacy/ota_4mb_v1.csv`) included: that is what lets a running hub rewrite its table in place. On 8 MB,
`app1` starts at 0x400000, above everything the 1.x table used.

A build variant compiles against the 4 MB table (the smaller app slot). `scripts/package_boards.py` then gives each
board its own `partition-table.bin` (written by `partitions.py`, byte-identical to `gen_esp32part.py`) and stamps the
board's flash size into the bootloader header, which the bootloader sizes the flash from.

## Config

The config lives in the `cfg` NVS partition as typed keys, one namespace per section. The config component
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

To add a section, define a value type with `ToDefault`, `LoadNvs`, `SaveNvs`, `FromJSON`, `ToJSON` and
`ToFlatbuffers`, and one `Config::Section<T>` for it in the module (`config/Section.h` has the contract). It joins
`jsonconfig`, `rawconfig`, the local protocol's Ready message and factory reset through the section registry.

Where the store is:

- `cfg` on a 2.x layout.
- On the 1.x layout, which has no `cfg`, the same namespaces in the shared `nvs` partition. The first boot that finds
  `cfg` copies them over key by key, compares the copy, and deletes the shared one.
- An empty store is filled once: from the 1.x littlefs config file (`/config` on the 1.x `config` partition, read
  only, so a rollback to 1.x still finds it) when there is one, else from every section's defaults.
- Without any usable NVS, the hub runs on defaults in RAM and logs it.

The shared `nvs` belongs to ESP-IDF (PHY calibration cache) and may be erased at any time; nothing of OpenShock's is
kept there once `cfg` exists. The WiFi driver's own NVS store is compiled out (`CONFIG_ESP_WIFI_NVS_ENABLED=n`).

## Portal assets

The captive portal's assets are one flat read-only image in the `www` partition (type 0x40, so no ESP-IDF component
tries to mount it), mapped once and served by pointer. `scripts/utils/wwwimage.py` writes it and
`components/www/include/www/WwwImage.h` documents the format: a sorted directory, 4-byte aligned bodies and a
truncated SHA-256 over everything after the header, checked before anything is served. The same image serves every
board. The OTA update skips flashing it when the hash on flash already matches the release's.

1.x-numbered releases also publish the assets as a littlefs image of the 1.x `static0` partition: that is what a 1.x
hub's OTA installs as `staticfs` and checks by mounting it. On the 1.x layout the portal serves from that image.

## The 1.x to 2.x migration

A 1.x hub reaches the 2.x layout through the 1.9.x pivot release, which runs on either table (`components/migration`):

1. On the 1.x table, once online: the target release (`CONFIG_OPENSHOCK_MIGRATION_TARGET_VERSION`, 2.0.0) must carry
   this board's partition table, and its app and www image must answer.
2. The table is downloaded into RAM, its SHA-256 checked, and it is checked against every invariant in
   `components/partition_layout`: nvs and otadata unmoved, `cfg` and `www` present, geometry the bootloader accepts,
   a slot for the running image, a 1.x live table, and a bootloader header that declares enough flash. Any failure
   leaves the hub on the 1.x layout and retries later.
3. Running from the 1.x `app1` on 4 MB, the image is first copied into `app0` and booted. On 8 MB it is copied to
   0x400000, where the new table puts `app1`.
4. The portal closes and the target's www image is written where the new table puts `www`, so the first boot of the
   new layout has a portal even without a network.
5. Config writes are held, NVS is closed, and the table sector is erased and written through the OTA path, read back,
   and the hub restarts.
6. On the 2.x table, still running the pivot: the config moves from `nvs` into `cfg`, the WiFi driver's stale store
   and any fragment of a 1.x image in the moved slot are erased, and the target version is installed. If it rolls back
   to the pivot, the chain stops.

`sysinfo` on the serial console prints the partition layout, the bootloader header's flash size and the config store.

### Recovery

The table sector's erase-to-write window (one sector, well under a second, once per hub) is the one step a power cut
cannot recover from: the bootloader then finds no partition table. The app, config and assets are still on flash, so
writing the board's 2.x table restores the hub with its config:

```sh
esptool write_flash 0x8000 partition-table.bin    # this board's, from its release
```

Flashing the merged image for the board at 0x0 also works but erases the config.
