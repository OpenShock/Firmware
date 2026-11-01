# Storage and partition layouts

How the firmware lays out flash and where it keeps its config and the captive portal's assets.

## Partition tables

One table, `partitions/ota_4mb.csv`, for every board whatever its flash size. Board files never name it.

| Partition | Offset | Size |
| --- | --- | --- |
| `nvs` | 0x9000 | 20 KiB |
| `otadata` | 0xE000 | 8 KiB |
| `app0`, `app1` | 0x10000, 0x1B0000 | 1,664 KiB each |
| `cfg` (data/nvs) | 0x350000 | 12 KiB |
| `www` (type 0x40) | 0x353000 | 628 KiB |
| `coredump` | 0x3F0000 | 64 KiB |

The flash past 4 MB on bigger boards stays unused.

This firmware only runs on this table. A hub on the 1.x table reaches it through the migration firmware on the
`migration/firmware-2.0` branch, which is the only place that code lives; only after that can it install a release from
here. Going back is not supported: the migration firmware treats a store without its `root/schema` marker as empty, so
a hub set up on this firmware would lose its config.

## Config

The config lives in the `cfg` NVS partition as typed keys, one namespace per section. At 12 KiB (three NVS pages, one
always kept free for compaction) it holds the whole config with some 32 long WiFi credentials; it fills up at about 36. The config component
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

Namespace and key names are at
most 15 characters and are permanent once hubs hold them: renaming one is a migration, not an edit. A missing key
reads as its default and an unknown key is ignored, so adding or removing a field needs no migration.

To add a section, define a value type whose default constructor is the defaults, with `Normalize` (its validation,
run on every value the section takes in), `LoadNvs`, `SaveNvs`, `FromJSON` (opened with `Parse::ObjectOrDefault`,
booleans through `Parse::JsonBool`) and `ToJSON`, and one `Config::Section<T>` for it in the module (the
`SectionValue` concept in `config/Section.h` is the contract). It joins `jsonconfig` and factory reset through the
section registry. The config has no FlatBuffers form: the portal's Ready message carries only the settings it shows.
A load opens its namespace with `Nvs::Handle::OpenForLoad`: a namespace never written reads every key as its
default, an open that failed fails the load, and a failed load runs the section on its defaults without trusting
flash. A save goes through `Nvs::Saver`, which writes only the members that differ from the previous value.

Where the store is:

- `cfg`. An empty store reads as the defaults; nothing is written until a value changes, so a value left at its
  default follows the firmware's default.
- Without a usable `cfg`, the hub runs on defaults in RAM and logs it.

The shared `nvs` belongs to ESP-IDF (PHY calibration cache) and may be erased at any time; nothing of OpenShock's is
kept there. The WiFi driver's own NVS store is compiled out (`CONFIG_ESP_WIFI_NVS_ENABLED=n`). `sysinfo` on the serial
console prints where the config is.

## Portal assets

The captive portal's assets are one flat read-only image in the `www` partition (type 0x40, so no ESP-IDF component
tries to mount it), mapped once and served by pointer. `scripts/utils/wwwimage.py` writes it and
`components/www/include/www/WwwImage.h` documents the format: a directory tree with sorted children, 4-byte aligned
gzipped bodies and a SHA-256 over the whole image (header included, its own field zeroed), checked before anything is
served.
The same image serves every board. The OTA update skips flashing it when the hash on flash already matches the
release's.

Releases publish the image as `www.bin` under the repository server's `staticfs` artifact.
