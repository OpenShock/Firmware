# Storage and partition layouts

How the firmware lays out flash, where it keeps the captive portal's assets, and how a 1.x hub's littlefs `static0`
partition becomes the `www` image partition in place. The reasoning behind each choice is in the design document
(OpenShock Firmware 2.0: Storage and Partition Migration Design); this page describes what the code does.

## Partition tables

One table, `partitions/ota_4mb.csv`, for every board whatever its flash size.

| Partition | Offset | Size | 1.x (`partitions/legacy/ota_4mb_v1.csv`) |
| --- | --- | --- | --- |
| `nvs` | 0x9000 | 20 KiB | the same |
| `otadata` | 0xE000 | 8 KiB | the same |
| `app0`, `app1` | 0x10000, 0x1B0000 | 1,664 KiB each | the same |
| `config` (data/spiffs) | 0x350000 | 12 KiB | the same |
| `www` (type 0x40) | 0x353000 | 628 KiB | littlefs `static0` (data/spiffs) |
| `coredump` | 0x3F0000 | 64 KiB | the same |

It is the 1.x geometry entry for entry: a fielded hub rewrites its table in place and nothing moves. Only `static0`
changes, in type and label. The flash past 4 MB on bigger boards stays unused.

## Portal assets

The captive portal's assets are one flat read-only image in the `www` partition (type 0x40, so no ESP-IDF component
tries to mount it), mapped once and served by pointer. `scripts/utils/wwwimage.py` writes it and
`components/www/include/www/WwwImage.h` documents the format: a sorted directory, 4-byte aligned bodies and a
SHA-256 over the whole image (header included, its own field zeroed), checked before anything is served.
The same image serves every board. The OTA update skips flashing it when the hash on flash already matches the
release's.

Releases publish the image as `www.bin` under the repository server's `staticfs` artifact. On the 1.x table, which has
no `www`, the portal still serves the littlefs `static0`, and the OTA update refuses before writing anything: such a
hub needs the migration below before it can take an update.

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

### Recovery

The table sector's erase-to-write window (one sector, well under a second, once per hub) is the one step a power cut
cannot recover from: the bootloader then finds no partition table. The app, config and assets are still on flash, so
writing the board's table restores the hub with its config:

```sh
esptool write_flash 0x8000 partition-table.bin    # this board's, from its release
```

Flashing the merged image for the board at 0x0 also works but erases the config.
