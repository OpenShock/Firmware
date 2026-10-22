#!/usr/bin/env python3
"""Merge each board's partition images into one flashable binary (native ESP-IDF).

Combines the discrete images produced by the C++ build (app, bootloader,
partition table) with the shared static0 web-assets image into a single binary
flashable at offset 0 - the web-flasher / GitHub-release artifact. Runs esptool
directly so the merge CI job needs esptool only, not a full ESP-IDF install.

Takes one board or many. --bindir and --output are templates expanded per board,
so a whole build merges in one invocation, in parallel, rather than one process
per board driven from the outside.

The partition table, app and static0 offsets are read from the partition CSV the
board's sdkconfig selects; the bootloader offset is the only chip-specific value. The
flash size written into the image header comes from the board's sdkconfig, so an
8 MB board's merged image does not tell its bootloader it has 4 MB.
Replaces the old chips/<chip>/merge-image.py scripts.
"""

import argparse
import io
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

import esptool

sys.path.insert(0, str(Path(__file__).resolve().parent))
from utils import partitions as partition_table  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# The 2nd-stage bootloader flashes at 0x0 on chips whose ROM loader expects it
# there, and at 0x1000 on the original ESP32 and ESP32-S2.
BOOTLOADER_OFFSET = {
    'esp32': '0x1000',
    'esp32s2': '0x1000',
    'esp32s3': '0x0',
    'esp32c3': '0x0',
}


# ESP-IDF's own default when no sdkconfig input picks a CONFIG_ESPTOOLPY_FLASHSIZE_* choice.
DEFAULT_FLASH_SIZE = '4MB'


def board_fragment(board: str) -> Path:
    fragment = ROOT / 'boards' / f'{board}.defaults'
    if not fragment.is_file():
        raise SystemExit(f'error: unknown board {board!r} ({fragment} missing)')
    return fragment


def resolve_target(board: str) -> str:
    fragment = board_fragment(board)
    for line in fragment.read_text(encoding='utf-8').splitlines():
        m = re.match(r'\s*CONFIG_IDF_TARGET\s*=\s*"([^"]+)"', line)
        if m:
            return m.group(1)
    raise SystemExit(f'error: {fragment} does not set CONFIG_IDF_TARGET')


def parse_flash_size(text: str) -> str | None:
    """The flash size an sdkconfig fragment selects, e.g. '8MB', or None when it picks none.

    It is a Kconfig choice, so the selected option reads CONFIG_ESPTOOLPY_FLASHSIZE_<size>=y.
    The last selection wins, as it would when sdkconfig layers the fragment.
    """
    size = None
    for line in text.splitlines():
        m = re.match(r'\s*CONFIG_ESPTOOLPY_FLASHSIZE_(\d+MB)\s*=\s*y\s*$', line)
        if m:
            size = m.group(1)
    return size


def resolve_flash_size(board: str) -> str:
    """The board's flash size, layered the way scripts/build.py layers sdkconfig: shared defaults, then the board."""
    size = None
    for path in (ROOT / 'sdkconfig.defaults', board_fragment(board)):
        if path.is_file():
            size = parse_flash_size(path.read_text(encoding='utf-8')) or size
    return size or DEFAULT_FLASH_SIZE


def merge_one(board: str, bindir: str, staticfs: str, output: str) -> tuple[str, bool, str]:
    """Merge one board, returning its captured output rather than printing it.

    Runs in a worker process: esptool keeps module-level state and exits the
    interpreter on failure, so a thread would take the whole run down with it.
    Output is captured so parallel boards do not interleave their logs into an
    unreadable braid; the caller prints each board's in one piece.
    """
    buf = io.StringIO()
    try:
        with redirect_stdout(buf), redirect_stderr(buf):
            chip = resolve_target(board)
            flash_size = resolve_flash_size(board)
            boot_offset = BOOTLOADER_OFFSET.get(chip)
            if boot_offset is None:
                raise SystemExit(f'error: no bootloader offset known for chip {chip!r} - add it to merge_image.py')

            table = partition_table.read_partitions(partition_table.partition_csv_for(board))
            if 'static0' not in table:
                raise SystemExit(f'error: the partition table for {board} has no static0 partition')
            table_offset    = hex(partition_table.partition_table_offset(board))
            app_offset      = hex(partition_table.first_app_partition(table)['offset'])
            staticfs_offset = hex(table['static0']['offset'])

            binpath = Path(bindir)
            argv = [
                '--chip', chip,
                'merge-bin',
                '--output', output,
                '--flash-size', flash_size,
                boot_offset, str(binpath / 'bootloader.bin'),
                table_offset, str(binpath / 'partition-table.bin'),
                app_offset, str(binpath / 'app.bin'),
                staticfs_offset, staticfs,
            ]
            print(f'+ esptool {" ".join(argv)}', flush=True)
            esptool.main(argv)
    except SystemExit as e:
        # esptool exits rather than raising; a zero code is still a success.
        # A string code is already the message, which is how resolve_target reports an unknown board.
        if e.code not in (0, None):
            detail = e.code if isinstance(e.code, str) else f'exited with {e.code}'
            return board, False, buf.getvalue() + f'\n{detail}'
    except Exception as e:
        return board, False, buf.getvalue() + f'\n{type(e).__name__}: {e}'

    return board, True, buf.getvalue()


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--board', required=True, action='append',
                    help='board name matching boards/<board>.defaults. Repeatable, and a single '
                         'value may hold several names separated by whitespace or newlines.')
    ap.add_argument('--bindir', required=True,
                    help='dir with app.bin, bootloader.bin, partition-table.bin. {board} is expanded per board.')
    ap.add_argument('--staticfs', required=True, help='path to the static0 image (staticfs.bin)')
    ap.add_argument('--output', required=True, help='merged output binary. {board} is expanded per board.')
    ap.add_argument('-j', '--jobs', type=int, default=0,
                    help='boards to merge at once. 0 (the default) uses one per CPU.')
    args = ap.parse_args()

    boards = [b for value in args.board for b in value.split()]
    if not boards:
        raise SystemExit('error: --board matched no board names')

    duplicates = {b for b in boards if boards.count(b) > 1}
    if duplicates:
        raise SystemExit(f'error: repeated board(s): {", ".join(sorted(duplicates))}')

    # Templates are expanded here so an unknown placeholder fails before any work starts.
    try:
        jobs = [(b, args.bindir.format(board=b), args.staticfs, args.output.format(board=b)) for b in boards]
    except KeyError as e:
        raise SystemExit(f'error: unknown placeholder {e} in --bindir or --output; only {{board}} is expanded')

    for _, _, _, output in jobs:
        Path(output).parent.mkdir(parents=True, exist_ok=True)

    workers = args.jobs if args.jobs > 0 else min(len(jobs), os.cpu_count() or 1)

    if workers == 1 or len(jobs) == 1:
        results = [merge_one(*job) for job in jobs]
    else:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            results = list(pool.map(merge_one, *zip(*jobs)))

    failed = []
    for board, ok, output in results:
        print(f'--- {board} ---', flush=True)
        if output:
            print(output.rstrip(), flush=True)
        if not ok:
            failed.append(board)

    if failed:
        print(f'error: failed to merge: {", ".join(failed)}', file=sys.stderr, flush=True)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
