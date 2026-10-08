"""Read the ESP-IDF partition table CSV a board builds with, so tools don't hard-code offsets and sizes."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

# ESP-IDF's default CONFIG_PARTITION_TABLE_OFFSET.
DEFAULT_PARTITION_TABLE_OFFSET = 0x8000


def _sdkconfig_value(board: str | None, key: str) -> str | None:
    """Last value of `key` in sdkconfig.defaults, then the board fragment (the order scripts/build.py layers them)."""
    paths = [ROOT / 'sdkconfig.defaults']
    if board is not None:
        paths.append(ROOT / 'boards' / f'{board}.defaults')

    value = None
    for path in paths:
        if not path.is_file():
            continue
        for line in path.read_text(encoding='utf-8').splitlines():
            m = re.match(rf'\s*{re.escape(key)}\s*=\s*"?([^"]*)"?\s*$', line)
            if m:
                value = m.group(1)
    return value


def partition_csv_for(board: str | None) -> Path:
    name = _sdkconfig_value(board, 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME')
    if not name:
        raise SystemExit(f'error: no CONFIG_PARTITION_TABLE_CUSTOM_FILENAME for board {board!r}')
    return ROOT / name


def partition_table_offset(board: str | None) -> int:
    value = _sdkconfig_value(board, 'CONFIG_PARTITION_TABLE_OFFSET')
    return int(value, 0) if value else DEFAULT_PARTITION_TABLE_OFFSET


def read_partitions(csv_path: Path) -> dict[str, dict]:
    """Partitions by name: {'type', 'subtype', 'offset', 'size'}. Every entry must give an explicit offset."""
    partitions = {}
    for raw in csv_path.read_text(encoding='utf-8').splitlines():
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(',')]
        if len(fields) < 5:
            raise SystemExit(f'error: malformed partition line in {csv_path}: {raw!r}')
        name, ptype, subtype, offset, size = fields[:5]
        if not offset:
            raise SystemExit(f'error: partition {name!r} in {csv_path} has no explicit offset')
        partitions[name] = {'type': ptype, 'subtype': subtype, 'offset': int(offset, 0), 'size': _parse_size(size)}
    return partitions


def _parse_size(size: str) -> int:
    m = re.fullmatch(r'(0x[0-9a-fA-F]+|\d+)([KM]?)', size)
    if not m:
        raise SystemExit(f'error: unsupported partition size {size!r}')
    value = int(m.group(1), 0)
    return value * {'': 1, 'K': 1024, 'M': 1024 * 1024}[m.group(2)]


def first_app_partition(partitions: dict[str, dict]) -> dict:
    apps = [p for p in partitions.values() if p['type'] == 'app']
    if not apps:
        raise SystemExit('error: partition table has no app partition')
    return min(apps, key=lambda p: p['offset'])
