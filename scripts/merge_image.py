#!/usr/bin/env python3
"""Merge a board's partition images into one flashable binary (native ESP-IDF).

Combines the discrete images produced by the C++ build (app, bootloader,
partition table) with the shared static0 web-assets image into a single binary
flashable at offset 0 - the web-flasher / GitHub-release artifact. Runs esptool
directly so the merge CI job needs esptool only, not a full ESP-IDF install.

Offsets mirror partitions/ota_4mb.csv (partition table 0x8000, app 0x10000,
static0 0x353000); the bootloader offset is the only chip-specific value.
Replaces the old chips/<chip>/merge-image.py scripts.
"""

import argparse
import re
from pathlib import Path

import esptool

ROOT = Path(__file__).resolve().parent.parent

# The 2nd-stage bootloader flashes at 0x0 on chips whose ROM loader expects it
# there, and at 0x1000 on the original ESP32 and ESP32-S2.
BOOTLOADER_OFFSET = {
    'esp32': '0x1000',
    'esp32s2': '0x1000',
    'esp32s3': '0x0',
    'esp32c3': '0x0',
}

# Fixed by partitions/ota_4mb.csv + the ESP-IDF default partition-table offset.
PARTITION_TABLE_OFFSET = '0x8000'
APP_OFFSET = '0x10000'
STATICFS_OFFSET = '0x353000'


def resolve_target(board: str) -> str:
    fragment = ROOT / 'boards' / f'{board}.defaults'
    if not fragment.is_file():
        raise SystemExit(f'error: unknown board {board!r} ({fragment} missing)')
    for line in fragment.read_text(encoding='utf-8').splitlines():
        m = re.match(r'\s*CONFIG_IDF_TARGET\s*=\s*"([^"]+)"', line)
        if m:
            return m.group(1)
    raise SystemExit(f'error: {fragment} does not set CONFIG_IDF_TARGET')


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--board', required=True, help='board name (matches boards/<board>.defaults)')
    ap.add_argument('--bindir', required=True, help='dir with app.bin, bootloader.bin, partition-table.bin')
    ap.add_argument('--staticfs', required=True, help='path to the static0 image (staticfs.bin)')
    ap.add_argument('--output', required=True, help='merged output binary')
    args = ap.parse_args()

    chip = resolve_target(args.board)
    boot_offset = BOOTLOADER_OFFSET.get(chip)
    if boot_offset is None:
        raise SystemExit(f'error: no bootloader offset known for chip {chip!r} - add it to merge_image.py')

    bindir = Path(args.bindir)
    argv = [
        '--chip', chip,
        'merge-bin',
        '--output', args.output,
        '--flash-size', '4MB',
        boot_offset, str(bindir / 'bootloader.bin'),
        PARTITION_TABLE_OFFSET, str(bindir / 'partition-table.bin'),
        APP_OFFSET, str(bindir / 'app.bin'),
        STATICFS_OFFSET, str(args.staticfs),
    ]
    print(f'+ esptool {" ".join(argv)}', flush=True)
    esptool.main(argv)


if __name__ == '__main__':
    main()
