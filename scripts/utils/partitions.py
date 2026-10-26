"""The ESP-IDF partition tables the firmware ships, as CSV and as the binary the bootloader reads.

There is one table per flash size (``partitions/ota_<size>.csv``); a board gets the one for its flash.
``build_table`` turns a CSV into the exact bytes ``gen_esp32part.py`` would produce (verified against
ESP-IDF 6.0's output for every table in ``partitions/``), so the packaging scripts need no ESP-IDF to
write a board's ``partition-table.bin``, and the host tests of the firmware's own table parser get
their fixtures from the same CSVs the device is flashed with.
"""

import hashlib
import re
import struct
from pathlib import Path
from typing import TypedDict

from .boards import flash_size, sdkconfig_value
from .paths import ROOT

# ESP-IDF's default CONFIG_PARTITION_TABLE_OFFSET.
DEFAULT_PARTITION_TABLE_OFFSET = 0x8000

# Binary format (components/bootloader_support/include/esp_flash_partitions.h): 32-byte entries, then an MD5
# entry over everything before it, then 0xFF up to ESP_PARTITION_TABLE_MAX_LEN.
ENTRY_MAGIC = 0x50AA
MD5_MAGIC = 0xEBEB
ENTRY_LEN = 32
TABLE_LEN = 0xC00
ENTRY_FORMAT = '<HBBII16sI'  # magic, type, subtype, offset, size, label, flags

PARTITION_TYPES = {'app': 0x00, 'data': 0x01, 'bootloader': 0x02, 'partition_table': 0x03}
APP_SUBTYPES = {'factory': 0x00, 'test': 0x20, **{f'ota_{n}': 0x10 + n for n in range(16)}}
DATA_SUBTYPES = {
    'ota': 0x00,
    'phy': 0x01,
    'nvs': 0x02,
    'coredump': 0x03,
    'nvs_keys': 0x04,
    'efuse': 0x05,
    'undefined': 0x06,
    'esphttpd': 0x80,
    'fat': 0x81,
    'spiffs': 0x82,
    'littlefs': 0x83,
}
FLAGS = {'encrypted': 1 << 0, 'readonly': 1 << 1}

APP_ALIGNMENT = 0x10000  # app partitions must start on a 64 KiB boundary (flash MMU page)
DATA_ALIGNMENT = 0x1000  # everything else on a sector


class Partition(TypedDict):
    type: str
    subtype: str
    offset: int
    size: int


def table_csv_for_flash_size(size: str) -> Path:
    """The table a flash size builds with: ``partitions/ota_4mb.csv`` up to 4 MB, ``ota_8mb.csv`` from 8 MB."""
    megabytes = int(size.removesuffix('MB'))
    if megabytes < 4:
        raise SystemExit(f'error: no partition table fits a {size} flash; the firmware needs at least 4 MB')
    return ROOT / 'partitions' / ('ota_8mb.csv' if megabytes >= 8 else 'ota_4mb.csv')


def partition_csv_for(board: str) -> Path:
    """The table a board is flashed with: picked by its flash size, never named in boards/<board>.defaults."""
    return table_csv_for_flash_size(flash_size(board))


def partition_table_offset(board: str) -> int:
    value = sdkconfig_value(board, 'CONFIG_PARTITION_TABLE_OFFSET')
    return int(value, 0) if value else DEFAULT_PARTITION_TABLE_OFFSET


def read_partitions(csv_path: Path) -> dict[str, Partition]:
    """Partitions by name. Every entry must give an explicit offset."""
    partitions: dict[str, Partition] = {}
    for raw in csv_path.read_text(encoding='utf-8').splitlines():
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(',')]
        if len(fields) < 5:
            raise SystemExit(f'error: malformed partition line in {csv_path}: {raw!r}')
        name, ptype, subtype, offset, size = fields[:5]
        where = f'partition {name!r} in {csv_path}'
        if not offset:
            raise SystemExit(f'error: {where} has no explicit offset')
        partitions[name] = {
            'type': ptype,
            'subtype': subtype,
            'offset': _parse_offset(offset, where),
            'size': _parse_size(size, where),
        }
    return partitions


def _parse_offset(offset: str, where: str) -> int:
    try:
        return int(offset, 0)
    except ValueError:
        raise SystemExit(f'error: {where} has a bad offset {offset!r}') from None


def _parse_size(size: str, where: str) -> int:
    m = re.fullmatch(r'(0x[0-9a-fA-F]+|\d+)([KM]?)', size)
    if not m:
        raise SystemExit(f'error: {where} has an unsupported size {size!r}')
    value = int(m.group(1), 0)
    return value * {'': 1, 'K': 1024, 'M': 1024 * 1024}[m.group(2)]


def first_app_partition(partitions: dict[str, Partition]) -> Partition:
    apps = [p for p in partitions.values() if p['type'] == 'app']
    if not apps:
        raise SystemExit('error: partition table has no app partition')
    return min(apps, key=lambda p: p['offset'])


def _type_code(ptype: str, where: str) -> int:
    if ptype in PARTITION_TYPES:
        return PARTITION_TYPES[ptype]
    try:
        code = int(ptype, 0)
    except ValueError:
        raise SystemExit(f'error: {where} has an unknown type {ptype!r}') from None
    if not 0 <= code <= 0xFE:
        raise SystemExit(f'error: {where} has a type out of range: {ptype}')
    return code


def _subtype_code(type_code: int, subtype: str, where: str) -> int:
    names = (
        APP_SUBTYPES
        if type_code == PARTITION_TYPES['app']
        else DATA_SUBTYPES
        if type_code == PARTITION_TYPES['data']
        else {}
    )
    if subtype in names:
        return names[subtype]
    try:
        code = int(subtype, 0) if subtype else 0
    except ValueError:
        raise SystemExit(f'error: {where} has an unknown subtype {subtype!r}') from None
    if not 0 <= code <= 0xFE:
        raise SystemExit(f'error: {where} has a subtype out of range: {subtype}')
    return code


def build_table(csv_path: Path, flash_size_bytes: int | None = None) -> bytes:
    """The binary table for a CSV: what gen_esp32part.py writes, MD5 entry included, 0xC00 bytes.

    Checks what the bootloader will check (alignment, overlap, order, bounds) so a bad CSV fails here
    rather than on a device.
    """
    partitions = read_partitions(csv_path)
    out = bytearray()
    previous_end = 0
    for name, part in partitions.items():
        where = f'partition {name!r} in {csv_path}'
        if len(name.encode('utf-8')) > 16:
            raise SystemExit(f'error: {where}: the label is longer than 16 bytes')
        type_code = _type_code(part['type'], where)
        subtype_code = _subtype_code(type_code, part['subtype'], where)
        alignment = APP_ALIGNMENT if type_code == PARTITION_TYPES['app'] else DATA_ALIGNMENT
        if part['offset'] % alignment:
            raise SystemExit(f'error: {where}: offset 0x{part["offset"]:X} is not {alignment // 1024} KiB aligned')
        if part['size'] <= 0 or part['size'] % DATA_ALIGNMENT:
            raise SystemExit(f'error: {where}: size 0x{part["size"]:X} is not a positive multiple of 4 KiB')
        if part['offset'] < previous_end:
            raise SystemExit(f'error: {where}: overlaps the partition before it (entries must be in offset order)')
        end = part['offset'] + part['size']
        if flash_size_bytes is not None and end > flash_size_bytes:
            raise SystemExit(
                f'error: {where}: ends at 0x{end:X}, past the {flash_size_bytes // (1024 * 1024)} MB flash'
            )
        previous_end = end
        out += struct.pack(
            ENTRY_FORMAT, ENTRY_MAGIC, type_code, subtype_code, part['offset'], part['size'], name.encode('utf-8'), 0
        )
    if len(out) + ENTRY_LEN > TABLE_LEN:
        raise SystemExit(f'error: {csv_path}: too many partitions for one table')
    out += struct.pack('<H', MD5_MAGIC) + b'\xff' * 14 + hashlib.md5(bytes(out)).digest()
    out += b'\xff' * (TABLE_LEN - len(out))
    return bytes(out)


def main(argv: list[str] | None = None) -> int:
    """``partitions.py <csv> <out.bin> [--flash-size 8MB]``: write a table, as scripts/package_boards.py does per board."""
    import argparse

    ap = argparse.ArgumentParser(description='Build an ESP-IDF partition-table binary from a CSV, without ESP-IDF')
    ap.add_argument('csv', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--flash-size', help='refuse entries past this size, e.g. 4MB')
    args = ap.parse_args(argv)
    size = int(args.flash_size.removesuffix('MB')) * 1024 * 1024 if args.flash_size else None
    args.output.write_bytes(build_table(args.csv, size))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
