#!/usr/bin/env python3
"""Merge each board's partition images into one flashable binary (native ESP-IDF).

Combines the discrete images produced by the C++ build (app, bootloader,
partition table) with the shared static0 web-assets image into a single binary
flashable at offset 0 - the web-flasher / GitHub-release artifact. Runs esptool
directly so the merge CI job needs esptool only, not a full ESP-IDF install.

Takes one board or many. --bindir and --output are templates expanded per board,
so a whole build merges in one invocation, in parallel, rather than one process
per board driven from the outside.

Offsets mirror partitions/ota_4mb.csv (partition table 0x8000, app 0x10000,
static0 0x353000); the bootloader offset is the only chip-specific value.
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
            boot_offset = BOOTLOADER_OFFSET.get(chip)
            if boot_offset is None:
                raise SystemExit(f'error: no bootloader offset known for chip {chip!r} - add it to merge_image.py')

            binpath = Path(bindir)
            argv = [
                '--chip', chip,
                'merge-bin',
                '--output', output,
                '--flash-size', '4MB',
                boot_offset, str(binpath / 'bootloader.bin'),
                PARTITION_TABLE_OFFSET, str(binpath / 'partition-table.bin'),
                APP_OFFSET, str(binpath / 'app.bin'),
                STATICFS_OFFSET, staticfs,
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
