#!/usr/bin/env python3
"""Merge each board's images into one binary flashable at offset 0 (the web flasher / release artifact).

    merge_image.py --board "A B" --bindir 'boards/{board}' --staticfs staticfs.bin --output 'merged/{board}.bin'

Takes a board's app, bootloader and partition table (in CI, what scripts/package_boards.py writes)
plus the shared static0 image. Offsets come from the board's partition CSV and esptool's bootloader
offset; the images are copied byte for byte (--flash-size keep). Needs esptool only, not ESP-IDF.
"""

import argparse
import sys
from pathlib import Path

import esptool
from esptool.targets import CHIP_DEFS

from utils import partitions
from utils.boards import board_fragment, board_names, read_target


def merge_one(board: str, bindir: Path, staticfs: str, output: str) -> None:
    chip = read_target(board_fragment(board))
    table = partitions.read_partitions(partitions.partition_csv_for(board))
    if 'static0' not in table:
        raise SystemExit(f'error: the partition table for {board} has no static0 partition')
    argv = [
        '--chip', chip, 'merge-bin', '--output', output, '--flash-size', 'keep',
        hex(CHIP_DEFS[chip].BOOTLOADER_FLASH_OFFSET), str(bindir / 'bootloader.bin'),
        hex(partitions.partition_table_offset(board)), str(bindir / 'partition-table.bin'),
        hex(partitions.first_app_partition(table)['offset']), str(bindir / 'app.bin'),
        hex(table['static0']['offset']), staticfs,
    ]  # fmt: skip
    print(f'+ esptool {" ".join(argv)}', flush=True)
    esptool.main(argv)


def main(argv=None) -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--board', required=True, action='append', help='board name(s); repeatable, whitespace-separated')
    ap.add_argument('--bindir', required=True, help='dir with app.bin, bootloader.bin, partition-table.bin; {board} is expanded')
    ap.add_argument('--staticfs', required=True, help='path to the static0 image (staticfs.bin)')
    ap.add_argument('--output', required=True, help='merged output binary; {board} is expanded')
    args = ap.parse_args(argv)

    names = board_names(args.board)
    # Expand both templates once up front, so an unknown placeholder fails before any work starts.
    try:
        for template in (args.bindir, args.output):
            template.format(board=names[0])
    except (KeyError, IndexError, ValueError) as e:
        raise SystemExit(f'error: cannot expand --bindir or --output ({type(e).__name__}: {e}); only {{board}} is expanded')

    failed = []
    for board in names:
        output = Path(args.output.format(board=board))
        output.parent.mkdir(parents=True, exist_ok=True)
        try:
            merge_one(board, Path(args.bindir.format(board=board)), args.staticfs, str(output))
        # esptool exits rather than raising, and a zero code is still a success.
        except SystemExit as e:
            if e.code not in (0, None):
                print(f'{board}: {e.code if isinstance(e.code, str) else f"esptool exited with {e.code}"}', file=sys.stderr)
                failed.append(board)
        except Exception as e:
            print(f'{board}: {type(e).__name__}: {e}', file=sys.stderr)
            failed.append(board)

    if failed:
        raise SystemExit(f'error: failed to merge: {", ".join(failed)}')


if __name__ == '__main__':
    main()
