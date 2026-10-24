#!/usr/bin/env python3
"""Turn per-variant builds into per-board images: stamp and verify each board in a copy of its variant's app.

    package_boards.py --variants images --board "OpenShock-Core-V2 Seeed-Xiao-ESP32S3" --out boards

reads <variants>/<variant-id>/{app,bootloader,partition-table}.bin, as CI's build jobs upload them, and
writes <out>/<board>/ with the stamped app beside the variant's bootloader and partition table, which
every board of a variant shares unchanged. Every board is attempted before the run fails.
"""

import argparse
import shutil
import sys
from pathlib import Path

from utils import boards, espimage, stamping
from utils.files import atomic_write_bytes


def package_board(board: str, variants: Path, out: Path) -> str:
    """Package one board into out/<board>, returning a one-line summary. Raises SystemExit on failure."""
    spec = boards.read_board(board)
    vid = boards.variant_id(boards.board_fragment(board))
    src = variants / vid
    if not src.is_dir():
        raise SystemExit(f'error: no build of variant {vid} in {variants}, which {board} needs')

    app_path = src / 'app.bin'
    stamped = stamping.stamp_image(espimage.load(app_path), spec, app_path)

    dst = out / board
    dst.mkdir(parents=True, exist_ok=True)
    app_out = dst / 'app.bin'
    atomic_write_bytes(app_out, stamped.data)
    # Check the file that ships, not the bytes in memory: it must be intact, for the board's chip and carry
    # exactly its descriptor, and stamping it again must change nothing, which proves the stamp idempotent.
    written = espimage.load(app_out)
    stamping.verify_image(written, spec, app_out)
    if stamping.stamp_image(written, spec, app_out).data != stamped.data:
        raise SystemExit(f'error: stamping {app_out} a second time changed it')
    for name in ('bootloader.bin', 'partition-table.bin'):
        shutil.copyfile(src / name, dst / name)
    return f'{board}: stamped from {vid}'


def package(board_names: list[str], variants: Path, out: Path) -> list[str]:
    """Package every board, printing each one's outcome. Returns the boards that failed."""
    failed = []
    for board in board_names:
        try:
            print(package_board(board, variants, out), flush=True)
        # ValueError covers espimage's ImageError and DescriptorError, and Descriptor.pack refusing a field.
        except (SystemExit, OSError, ValueError) as err:
            message = err.code if isinstance(err, SystemExit) else f'error: {type(err).__name__}: {err}'
            print(f'{board}: {message}', file=sys.stderr, flush=True)
            failed.append(board)
    return failed


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--variants', required=True, type=Path, help='directory holding one <variant-id>/ per build')
    ap.add_argument(
        '--board',
        required=True,
        action='append',
        help='board to package. Repeatable, and one value may hold several names separated by whitespace.',
    )
    ap.add_argument('--out', required=True, type=Path, help='directory to write <board>/ into')
    args = ap.parse_args(argv)

    failed = package(boards.board_names(args.board), args.variants, args.out)
    if failed:
        print(f'error: failed to package: {", ".join(failed)}', file=sys.stderr, flush=True)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
