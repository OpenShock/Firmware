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
| `led` | device_control (`visual/LedConfig.h`) | `led` |

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
`components/www/include/www/WwwImage.h` parses it: a directory tree with sorted children, 4-byte aligned gzipped
bodies and a SHA-256 over the whole image (header included, its own field zeroed), checked before anything is served.
The same image serves every board. The OTA update skips flashing it when the hash on flash already matches the
release's.

Releases publish the image as `www.bin` under the repository server's `staticfs` artifact.

### www image format

All fields are little-endian and everything is 4-byte aligned. The layout, as an
[ImHex](https://github.com/WerWolv/ImHex) pattern (paste it into the Pattern Editor, or save it as `www.hexpat`; it
needs ImHex's standard library, which ships with ImHex and lives in
[ImHex-Patterns](https://github.com/WerWolv/ImHex-Patterns/tree/master/includes)):

```rust
#pragma description OpenShock www image (captive portal assets)
#pragma magic [ 4F 53 57 57 ] @ 0x00
#pragma endian little

import std.mem;
import std.sys;
import std.string;
import type.magic;

bitfield NodeFlags {
    directory : 1;
    immutable : 1;  // files only: content-hashed name, long browser cache
    padding   : 14; // must be zero
};

struct Header {
    type::Magic<"OSWW"> magic;
    u16 version;
    u16 node_count;  // root and directories included
    u32 dir_len;     // nodes + string table + padding to 4
    u32 image_len;   // header to the end of the last body's padding
    u8  image_hash[32] [[sealed, comment("SHA-256 of the first image_len bytes, this field zeroed")]];
    padding[16];     // reserved, zero
};

Header header @ 0x00;
std::assert(header.version == 1, "unsupported version");
std::assert(header.image_len <= std::mem::size(), "image_len past the end of the data");

u32 strings_start = 0x40 + 0x10 * header.node_count;
u32 body_start    = 0x40 + header.dir_len;
std::assert_warn(header.dir_len % 4 == 0 && strings_start <= body_start, "bad dir_len");
std::assert_warn(header.image_len % 4 == 0, "image_len not 4-byte aligned");

fn node_label(ref auto node) {
    if (node.flags.directory) {
        if (std::string::length(node.name) == 0) return "/";
        return std::format("{}/  [{} children @ node {}]", node.name, node.size, node.offset);
    }
    return std::format("{}  [{} bytes{}]", node.name, node.size, node.flags.immutable ? ", immutable" : "");
};

struct Node {
    u16       name_off;  // into the string table
    NodeFlags flags;
    u32       offset;    // directory: index of its first child; file: absolute body offset
    u32       size;      // directory: number of children; file: body bytes
    u32       reserved;  // zero
    std::string::NullString name @ strings_start + name_off;

    if (flags.directory) {
        std::assert_warn(!flags.immutable, "immutable directory");
    } else {
        std::assert_warn(offset >= body_start && offset % 4 == 0 && offset + size <= header.image_len, "body out of range");
        u8 body[size] @ offset [[sealed, comment("gzip")]];
    }
} [[format("node_label")]];

Node nodes[header.node_count] @ 0x40;
char strings[body_start - strings_start] @ strings_start [[sealed]];
u8   trailing[std::mem::size() - header.image_len] @ header.image_len [[sealed, comment("partition space after the image")]];
```

The rules a valid image follows. The pattern warns on a few; the firmware (`Www::Image::Parse`) enforces rules 1-5 and
the part of rule 6 marked as checked, in this order:

1. `image_len` is 4-aligned, at least the 64-byte header and within the partition.
2. `image_hash` is the SHA-256 of the first `image_len` bytes with `image_hash` zeroed. Nothing past the version and
   `image_len` is read before this matches.
3. Reserved bytes (header and nodes) and unknown flag bits are zero.
4. Node 0 is the root: a directory with an empty name. Every other name is one non-empty path component, no `/`, no
   `.` or `..`, no ASCII uppercase (the packer folds names to lowercase; lookups fold the request path the same way).
5. A directory's children are one contiguous run of nodes, strictly sorted by name (bytewise), so lookups binary search
   each level. The runs are laid out breadth-first: walking the nodes in order, each directory's run starts where the
   previous one ended, the first at node 1, and together they cover every node.
6. Bodies lie between the end of the directory and `image_len`, 4-aligned, in node order, each padded to 4 bytes;
   `image_len` is exactly the end of the last one's padding. Every body is gzipped (there is no flag for it).
   *Checked:* each body is 4-aligned and within that range, and `image_len` is the furthest padded body end.
   *Producer guarantee only* (the packer writes them; the parser does not check): bodies are in node order and do not
   overlap, and their contents are gzip. The image hash already rules out a body changed after packing.

The packer's output is a pure function of the assets (sorted, no timestamps), so an unchanged frontend gives a
byte-identical image. `scripts/utils/wwwimage.py`, `WwwImage.h` and this section describe the same format: change them
together.
