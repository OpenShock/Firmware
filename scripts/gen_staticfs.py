#!/usr/bin/env python3
"""Build the captive-portal asset image (www.bin) for the `www` partition.

Pipeline:
  1. Build the SvelteKit frontend (pnpm) when its sources are newer than the last build (unless --skip-frontend-build, which CI passes).
  2. Gzip every built asset (the frontend build emits plain files; compression happens here).
  3. Pack them into one read-only image (scripts/utils/wwwimage.py), checked to fit the smallest `www` partition.

The output is reproducible: gzip headers carry no timestamp and entries are sorted, so an unchanged frontend gives a
byte-identical www.bin (and the same hash, so devices don't re-flash it). One image serves every board: it is not
padded to any partition size, only checked to fit.

With --legacy-output it also packs the same assets as a littlefs image of the 1.x `static0` partition
(partitions/legacy/ota_4mb_v1.csv, under /www/<path>.gz). That is what a 1.x hub's OTA installs as the `staticfs`
artifact and verifies by mounting it, so it is published for 1.x-numbered releases (the 1.9.x pivot) only; it goes
away with release/1.9.

Invoked by CMake (see the root CMakeLists.txt), and by CI with --skip-frontend-build.
"""

import argparse
import gzip
import io
import os
import shutil
import subprocess
import sys
from collections.abc import Iterable
from pathlib import Path

from utils import partitions, wwwimage
from utils.partitions import WWW_PARTITION
from utils.paths import ROOT

# The littlefs geometry the 1.x firmware (and this firmware on the 1.x layout) mounts static0 with (components/fs/src/
# LfsPartition.cpp). Only the block size is on disk and checked at mount; the rest are runtime buffer sizes.
LEGACY_PARTITION = 'static0'
LEGACY_TABLE = ROOT / 'partitions' / 'legacy' / 'ota_4mb_v1.csv'
LFS_BLOCK_SIZE = 4096
LFS_READ_SIZE = LFS_PROG_SIZE = 128
LFS_CACHE_SIZE = 512
LFS_LOOKAHEAD_SIZE = 128

# Inputs of the frontend build; a change to any of them makes frontend/build stale.
FRONTEND_INPUTS = (
    'src',
    'static',
    'packages',
    'index.html',
    'package.json',
    'pnpm-lock.yaml',
    'vite.config.ts',
    'tsconfig.json',
)
FRONTEND_IGNORED_DIRS = {'node_modules', 'build', 'dist', '.svelte-kit'}


def log(msg: str) -> None:
    print(f'[gen_staticfs] {msg}', flush=True)


def newest_mtime(paths: Iterable[Path]) -> float:
    newest = 0.0
    for path in paths:
        if path.is_file():
            newest = max(newest, path.stat().st_mtime)
            continue
        for root, dirs, files in os.walk(path):
            dirs[:] = [d for d in dirs if d not in FRONTEND_IGNORED_DIRS]
            for name in files:
                newest = max(newest, (Path(root) / name).stat().st_mtime)
    return newest


def frontend_build_is_current(frontend_dir: Path, build_dir: Path) -> bool:
    index = build_dir / 'index.html'
    if not index.is_file():
        return False
    inputs = [frontend_dir / p for p in FRONTEND_INPUTS if (frontend_dir / p).exists()]
    return newest_mtime(inputs) <= index.stat().st_mtime


def run_pnpm(pnpm: str, args: list[str], frontend_dir: Path) -> None:
    try:
        subprocess.run([pnpm, *args], cwd=frontend_dir, check=True)
    except subprocess.CalledProcessError as err:
        raise SystemExit(f'error: `pnpm {" ".join(args)}` failed in {frontend_dir} (exit {err.returncode})') from None


def build_frontend(frontend_dir: Path) -> Path:
    """Run the SvelteKit production build when frontend/build is missing or older than its sources."""
    build_dir = frontend_dir / 'build'
    if frontend_build_is_current(frontend_dir, build_dir):
        log('frontend/build is up to date, skipping pnpm')
        return build_dir

    # Resolved here rather than through a shell: on Windows pnpm is a pnpm.cmd shim, which which() finds.
    pnpm = shutil.which('pnpm')
    if pnpm is None:
        raise SystemExit('error: pnpm not found on PATH; install Node.js and pnpm, or pass --skip-frontend-build')

    log('Building frontend (pnpm i && pnpm run build)...')
    run_pnpm(pnpm, ['i'], frontend_dir)
    run_pnpm(pnpm, ['run', 'build'], frontend_dir)
    log('Frontend build complete')
    return build_dir


def gzip_bytes(raw: bytes) -> bytes:
    # mtime=0 and no filename: the gzip bytes depend only on the content. GzipFile, not gzip.compress: since 3.11 the
    # latter goes through zlib and its OS header byte is platform-dependent, which would break byte-identical images.
    out = io.BytesIO()
    with gzip.GzipFile(filename='', mode='wb', fileobj=out, mtime=0) as gz:
        gz.write(raw)
    return out.getvalue()


def collect_assets(build_dir: Path) -> list[wwwimage.Entry]:
    """Every file under build_dir, gzipped, as an entry at its relative path."""
    entries = wwwimage.read_tree(build_dir, gzip_bytes)
    if not any(e.path == 'index.html' for e in entries):
        raise SystemExit(f'error: {build_dir}/index.html missing: the frontend build produced no index.html')
    return entries


def www_partition_size(csv_path: Path) -> int:
    return partitions.www_partition(partitions.read_partitions(csv_path), str(csv_path))['size']


def pack_image(entries: list[wwwimage.Entry], output: Path, max_size: int) -> None:
    try:
        image = wwwimage.pack(entries)
    except wwwimage.WwwImageError as err:
        raise SystemExit(f'error: cannot pack the assets: {err}') from None
    if len(image) > max_size:
        raise SystemExit(
            f'error: the asset image is {len(image)} bytes, more than the {max_size}-byte {WWW_PARTITION} partition'
        )
    output.resolve().parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    log(
        f'Packed {len(entries)} assets into {output} ({len(image)} of {max_size} bytes, {100 * len(image) // max_size}% of the partition)'
    )


def pack_legacy_image(entries: list[wwwimage.Entry], output: Path, partition_size: int) -> None:
    """The assets as the 1.x littlefs image: /www/<path>.gz, exactly partition_size bytes."""
    try:
        from littlefs import LittleFS
    except ImportError:
        raise SystemExit(
            f'error: littlefs-python is not installed for {sys.executable}: the build scripts need an interpreter '
            'with requirements.txt installed (docs/DEVELOPING.md, Python dependencies)'
        ) from None

    if partition_size % LFS_BLOCK_SIZE != 0:
        raise SystemExit(f'error: partition size {partition_size} is not a multiple of block size {LFS_BLOCK_SIZE}')
    fs = LittleFS(
        block_size=LFS_BLOCK_SIZE,
        block_count=partition_size // LFS_BLOCK_SIZE,
        read_size=LFS_READ_SIZE,
        prog_size=LFS_PROG_SIZE,
        cache_size=LFS_CACHE_SIZE,
        lookahead_size=LFS_LOOKAHEAD_SIZE,
    )
    for entry in entries:
        path = f'www/{entry.path}.gz'
        fs.makedirs(path.rsplit('/', 1)[0], exist_ok=True)
        with fs.open(path, 'wb') as f_out:
            f_out.write(entry.data)
    output.resolve().parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bytes(fs.context.buffer))
    log(f'Packed {len(entries)} assets into the 1.x littlefs image {output} ({partition_size} bytes)')


def legacy_partition_size() -> int:
    return partitions.read_partitions(LEGACY_TABLE)[LEGACY_PARTITION]['size']


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description='Build the OpenShock captive-portal asset image (www.bin)')
    p.add_argument('--frontend-dir', type=Path, required=True, help='Path to the frontend/ project')
    p.add_argument('--output', type=Path, required=True, help='Output image path (www.bin)')
    size = p.add_argument_group('size limit (at least one)')
    size.add_argument(
        '--max-size', type=lambda v: int(v, 0), help=f'largest image allowed ({WWW_PARTITION} partition size)'
    )
    size.add_argument(
        '--partition-csv',
        type=Path,
        action='append',
        default=[],
        help=f'partition table CSV to read the {WWW_PARTITION} size from; repeatable, the smallest wins',
    )
    p.add_argument(
        '--skip-frontend-build', action='store_true', help='Reuse existing frontend/build without invoking pnpm'
    )
    p.add_argument(
        '--legacy-output',
        type=Path,
        help=f'also write the assets as the 1.x littlefs {LEGACY_PARTITION} image here (1.x-numbered releases)',
    )
    args = p.parse_args(argv)

    limits = [www_partition_size(csv) for csv in args.partition_csv]
    if args.max_size is not None:
        limits.append(args.max_size)
    if not limits:
        p.error('one of --max-size or --partition-csv is required')

    if args.skip_frontend_build:
        build_dir = args.frontend_dir / 'build'
        if not build_dir.is_dir():
            raise SystemExit(f'error: --skip-frontend-build set but {build_dir} does not exist')
    else:
        build_dir = build_frontend(args.frontend_dir)

    entries = collect_assets(build_dir)
    log(f'Collected {len(entries)} gzipped assets from {build_dir}')
    pack_image(entries, args.output, min(limits))
    if args.legacy_output is not None:
        pack_legacy_image(entries, args.legacy_output, legacy_partition_size())
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
