# Storage and partition layouts

How the firmware lays out flash, where it keeps its config and portal assets, and how a 1.x hub moves to the 2.x
layout over the air. The reasoning behind each choice is in the design document (OpenShock Firmware 2.0: Storage and
Partition Migration Design); this page describes what the code does.

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

It is the 1.x geometry entry for entry: a fielded hub rewrites its table in place and nothing moves, so the migration
never copies a firmware image and never needs more flash than the 1.x table already declared. Only `config` and
`static0` change, in type and label. The flash past 4 MB on bigger boards stays unused.

`scripts/package_boards.py` gives each board its `partition-table.bin` (written by `scripts/utils/partitions.py`,
byte-identical to `gen_esp32part.py`) and stamps the board's flash size into the bootloader header, which the
bootloader sizes the flash from.

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

The `estop` namespace also holds the E-Stop latch, `latched` (u8), which is state rather than config: it is not in
`jsonconfig` or the wire form. The E-Stop task writes it when the E-Stop activates and when it is cleared with the
button, after the change is already in effect. At boot it is read before anything can transmit, and a set latch keeps the
E-Stop active until it is cleared with the button. A missing key reads as not latched, so a fresh hub starts usable, and
a read error reads as latched. A latch on a hub whose E-Stop is disabled is dropped, since no button could clear it.
A factory reset clears it. A latch set while another config write holds the store (the migration's table write, for
one) reaches flash only once that write is done.

The store's own namespace is `root`, with `schema` (u8, 1) marking a filled store. Namespace and key names are at
most 15 characters and are permanent once hubs hold them: renaming one is a migration, not an edit. A missing key
reads as its default and an unknown key is ignored, so adding or removing a field never changes the schema.

To add a section, define a value type with `ToDefault`, `LoadNvs`, `SaveNvs`, `FromJSON`, `ToJSON` and
`ToFlatbuffers`, and one `Config::Section<T>` for it in the module (`config/Section.h` has the contract). It joins
`jsonconfig`, `rawconfig`, the local protocol's Ready message and factory reset through the section registry.

Where the store is:

- `cfg` on the 2.x layout. On the first boot after the table rewrite `cfg` still holds the 1.x littlefs; it is erased
  before NVS opens it.
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
truncated SHA-256 over the whole image (header included, its own field zeroed), checked before anything is served. The same image serves every
board. The OTA update skips flashing it when the hash on flash already matches the release's.

A 1.x hub's OTA only accepts a `staticfs` it can mount as littlefs, so the 1.x bridge release (the 1.9.x pivot) also
publishes the assets as a littlefs image of `static0`, and on the 1.x layout its portal serves from that. CI builds the
littlefs image for 1.x-numbered versions only; every other release ships the www image alone.

## The 1.x to 2.x migration

A 1.x hub reaches the 2.x layout through the 1.9.x pivot release, which runs on either table (`components/migration`):

1. On the 1.x table, once online: the target release (`CONFIG_OPENSHOCK_MIGRATION_TARGET_VERSION`, 2.0.0) must carry
   this board's partition table, and its app and www image must answer.
2. The table is downloaded into RAM, its SHA-256 checked, and it is checked against every invariant in
   `components/partition_layout`: nvs and otadata unmoved, `cfg` and `www` present, geometry the bootloader accepts,
   a slot for the running image, a 1.x live table, and a bootloader header that declares enough flash. Any failure
   leaves the hub on the 1.x layout and retries later.
   The new table must also keep every entry where it is and retype nothing but `config` and `static0`.
3. The portal closes and the target's www image is written over `static0`, which the new table makes `www`, so the
   first boot of the new layout has a portal even without a network.
4. Config writes are held, NVS is closed, and the table sector is erased and written through the OTA path, read back,
   and the hub restarts.
5. On the 2.x table, still running the pivot: the 1.x littlefs in `cfg` is erased, the config moves from `nvs` into
   `cfg`, the WiFi driver's stale store is erased, and the target version is installed. If it rolls back to the
   pivot, the chain stops.

`sysinfo` on the serial console prints the partition layout, the bootloader header's flash size and the config store.

### Recovery

The table sector's erase-to-write window (one sector, well under a second, once per hub) is the one step a power cut
cannot recover from: the bootloader then finds no partition table. The app, config and assets are still on flash, so
writing the board's 2.x table restores the hub with its config:

```sh
esptool write_flash 0x8000 partition-table.bin    # this board's, from its release
```

Flashing the merged image for the board at 0x0 also works but erases the config.
