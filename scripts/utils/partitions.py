"""Read the ESP-IDF partition table CSV a board builds with, so tools don't hard-code offsets and sizes."""

import re
from pathlib import Path
from typing import TypedDict

from .boards import sdkconfig_value
from .paths import ROOT

# ESP-IDF's default CONFIG_PARTITION_TABLE_OFFSET.
DEFAULT_PARTITION_TABLE_OFFSET = 0x8000


class Partition(TypedDict):
    type: str
    subtype: str
    offset: int
    size: int


def partition_csv_for(board: str) -> Path:
    name = sdkconfig_value(board, 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME')
    if not name:
        raise SystemExit(f'error: no CONFIG_PARTITION_TABLE_CUSTOM_FILENAME for board {board!r}')
    return ROOT / name


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
