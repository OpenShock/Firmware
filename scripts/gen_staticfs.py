#!/usr/bin/env python3
"""Build the captive-portal static filesystem image (native ESP-IDF).

Pipeline:
  1. Build the SvelteKit frontend (pnpm) when its sources are newer than the last build (unless --skip-frontend-build, which CI passes).
  2. Gzip every built asset into <staging>/www/<path>.gz.
  3. Pack <staging>/www into a littlefs image sized for the `static0` partition.

The output is reproducible: gzip headers carry no timestamp and files are packed in sorted order, so an unchanged
frontend gives a byte-identical staticfs.bin (and the same hash, so devices don't re-flash it).

Invoked by CMake (see the root CMakeLists.txt), and by CI with --skip-frontend-build.
"""

import argparse
import gzip
import os
import shutil
import subprocess
import sys
from collections.abc import Iterable
from pathlib import Path

from utils import partitions

# The littlefs geometry the firmware mounts static0 with (components/fs/src/LfsPartition.cpp). Only the
# block size is on disk and checked at mount; read/prog/cache are runtime buffer sizes that must agree.
BLOCK_SIZE = 4096
READ_SIZE = PROG_SIZE = 128
CACHE_SIZE = 512
LOOKAHEAD_SIZE = 128

STATIC_PARTITION = 'static0'

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


def stage_assets(build_dir: Path, staging_dir: Path) -> Path:
    """Mirror build_dir into <staging>/www, gzipping everything that isn't already .gz."""
    www_dir = staging_dir / 'www'
    if www_dir.exists():
        shutil.rmtree(www_dir)

    count = 0
    for root, dirs, files in os.walk(build_dir):
        dirs.sort()
        dst_root = www_dir / Path(root).relative_to(build_dir)
        dst_root.mkdir(parents=True, exist_ok=True)

        for name in sorted(files):
            src = Path(root) / name
            if name.endswith('.gz'):
                shutil.copyfile(src, dst_root / name)
            else:
                # mtime=0: the gzip bytes depend only on the content
                with (
                    (dst_root / f'{name}.gz').open('wb') as raw,
                    gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as gz,
                ):
                    gz.write(src.read_bytes())
            count += 1

    log(f'Staged {count} gzipped assets into {www_dir}')
    if not (www_dir / 'index.html.gz').exists():
        raise SystemExit(f'error: {www_dir}/index.html.gz missing: the frontend build produced no index.html')
    return www_dir


def pack_image(staging_dir: Path, output: Path, partition_size: int) -> None:
    """Pack <staging_dir>/www into a littlefs image of exactly partition_size bytes (as /www/...)."""
    try:
        from littlefs import LittleFS
    except ImportError:
        raise SystemExit(
            f'error: littlefs-python is not installed for {sys.executable}: the build scripts need an interpreter '
            'with requirements.txt installed (docs/DEVELOPING.md, Python dependencies)'
        ) from None

    if partition_size % BLOCK_SIZE != 0:
        raise SystemExit(f'error: partition size {partition_size} is not a multiple of block size {BLOCK_SIZE}')
    block_count = partition_size // BLOCK_SIZE
    fs = LittleFS(
        block_size=BLOCK_SIZE,
        block_count=block_count,
        read_size=READ_SIZE,
        prog_size=PROG_SIZE,
        cache_size=CACHE_SIZE,
        lookahead_size=LOOKAHEAD_SIZE,
    )

    # Only www/ is packed, so stray files in the staging directory never end up in the image.
    file_count = 0
    for root, dirs, files in os.walk(staging_dir / 'www'):
        dirs.sort()
        rel_root = Path(root).relative_to(staging_dir).as_posix()
        fs.makedirs(rel_root, exist_ok=True)
        for name in sorted(files):
            with fs.open(f'{rel_root}/{name}', 'wb') as f_out:
                f_out.write((Path(root) / name).read_bytes())
            file_count += 1

    output.resolve().parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bytes(fs.context.buffer))

    log(f'Packed {file_count} files into {output} ({partition_size} bytes, {block_count} x {BLOCK_SIZE}-byte blocks)')


def static_partition_size(csv_path: Path) -> int:
    table = partitions.read_partitions(csv_path)
    if STATIC_PARTITION not in table:
        raise SystemExit(f'error: {csv_path} has no {STATIC_PARTITION} partition')
    return table[STATIC_PARTITION]['size']


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description='Build the OpenShock static filesystem image')
    p.add_argument('--frontend-dir', type=Path, required=True, help='Path to the frontend/ project')
    p.add_argument('--staging-dir', type=Path, required=True, help='Where to stage gzipped assets (contains www/)')
    p.add_argument('--output', type=Path, required=True, help='Output littlefs image path')
    size = p.add_mutually_exclusive_group(required=True)
    size.add_argument('--partition-size', type=lambda v: int(v, 0), help=f'{STATIC_PARTITION} partition size')
    size.add_argument(
        '--partition-csv', type=Path, help=f'partition table CSV to read the {STATIC_PARTITION} size from'
    )
    p.add_argument(
        '--skip-frontend-build', action='store_true', help='Reuse existing frontend/build without invoking pnpm'
    )
    args = p.parse_args(argv)

    if args.skip_frontend_build:
        build_dir = args.frontend_dir / 'build'
        if not build_dir.is_dir():
            raise SystemExit(f'error: --skip-frontend-build set but {build_dir} does not exist')
    else:
        build_dir = build_frontend(args.frontend_dir)

    partition_size = args.partition_size
    if partition_size is None:
        partition_size = static_partition_size(args.partition_csv)

    stage_assets(build_dir, args.staging_dir)
    pack_image(args.staging_dir, args.output, partition_size)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
