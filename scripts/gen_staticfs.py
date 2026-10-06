#!/usr/bin/env python3
"""Build the captive-portal static filesystem image (native ESP-IDF).

Pipeline:
  1. Build the SvelteKit frontend (pnpm) when its sources are newer than the last build (never in CI).
  2. Gzip every built asset into <staging>/www/<path>.gz.
  3. Pack <staging>/www into a littlefs image sized for the `static0` partition.

The output is reproducible: gzip headers carry no timestamp and files are packed in sorted order, so an unchanged
frontend gives a byte-identical staticfs.bin (and the same hash, so devices don't re-flash it).

The littlefs geometry MUST match how the firmware mounts the partition at
runtime (components/fs/src/LfsPartition.cpp): 4 KiB erase blocks, 128-byte
read/prog. Only block_size and block_count are fixed on-disk and validated
against the superblock at mount; the rest are runtime buffer sizes.

Invoked by CMake (see the root CMakeLists.txt). This replaces the old
PlatformIO SCons extra_script scripts/build_frontend.py.
"""

import argparse
import gzip
import os
import shutil
import subprocess
import sys


def log(msg):
    print(f'[gen_staticfs] {msg}', flush=True)


# Inputs of the frontend build; a change to any of them makes frontend/build stale.
FRONTEND_INPUTS = ('src', 'static', 'packages', 'index.html', 'package.json', 'pnpm-lock.yaml', 'vite.config.ts', 'tsconfig.json')
FRONTEND_IGNORED_DIRS = {'node_modules', 'build', 'dist', '.svelte-kit'}


def newest_mtime(paths):
    newest = 0.0
    for path in paths:
        if os.path.isfile(path):
            newest = max(newest, os.path.getmtime(path))
            continue
        for root, dirs, files in os.walk(path):
            dirs[:] = [d for d in dirs if d not in FRONTEND_IGNORED_DIRS]
            for name in files:
                newest = max(newest, os.path.getmtime(os.path.join(root, name)))
    return newest


def frontend_build_is_current(frontend_dir, build_dir):
    index = os.path.join(build_dir, 'index.html')
    if not os.path.isfile(index):
        return False
    inputs = [os.path.join(frontend_dir, p) for p in FRONTEND_INPUTS if os.path.exists(os.path.join(frontend_dir, p))]
    return newest_mtime(inputs) <= os.path.getmtime(index)


def build_frontend(frontend_dir):
    """Run the SvelteKit production build when frontend/build is missing or older than its sources.

    In CI the frontend is built once in a separate job and downloaded as an
    artifact into frontend/build, so we must not rebuild it here.
    """
    build_dir = os.path.join(frontend_dir, 'build')

    if os.environ.get('CI'):
        if not os.path.isdir(build_dir):
            raise SystemExit(f'CI build: expected prebuilt frontend at {build_dir}')
        log('CI detected — using prebuilt frontend/build')
        return build_dir

    if frontend_build_is_current(frontend_dir, build_dir):
        log('frontend/build is up to date, skipping pnpm')
        return build_dir

    log('Building frontend (pnpm i && pnpm run build)...')
    subprocess.run(['pnpm', 'i'], cwd=frontend_dir, check=True, shell=os.name == 'nt')
    subprocess.run(['pnpm', 'run', 'build'], cwd=frontend_dir, check=True, shell=os.name == 'nt')
    log('Frontend build complete')
    return build_dir


def stage_assets(build_dir, staging_dir):
    """Mirror build_dir into <staging>/www, gzipping everything that isn't already .gz."""
    www_dir = os.path.join(staging_dir, 'www')
    if os.path.exists(www_dir):
        shutil.rmtree(www_dir)

    count = 0
    for root, dirs, files in os.walk(build_dir):
        dirs.sort()
        rel = os.path.relpath(root, build_dir)
        dst_root = www_dir if rel == '.' else os.path.join(www_dir, rel)
        os.makedirs(dst_root, exist_ok=True)

        for name in sorted(files):
            src = os.path.join(root, name)
            if name.endswith('.gz'):
                dst = os.path.join(dst_root, name)
                shutil.copyfile(src, dst)
            else:
                dst = os.path.join(dst_root, name + '.gz')
                # mtime=0 and no filename in the header: the gzip bytes depend only on the content
                with open(src, 'rb') as f_in, open(dst, 'wb') as raw_out:
                    with gzip.GzipFile(filename='', mode='wb', fileobj=raw_out, mtime=0) as f_out:
                        f_out.write(f_in.read())
            count += 1

    log(f'Staged {count} gzipped assets into {www_dir}')
    if not os.path.exists(os.path.join(www_dir, 'index.html.gz')):
        raise SystemExit(f'{www_dir}/index.html.gz missing — frontend build produced no index.html')
    return www_dir


def pack_image(staging_dir, output, partition_size, block_size, read_size, prog_size):
    """Pack <staging_dir>/www into a littlefs image of exactly partition_size bytes (as /www/...)."""
    try:
        from littlefs import LittleFS
    except ImportError:
        raise SystemExit("littlefs-python is required (pip install -r requirements.txt)")

    if partition_size % block_size != 0:
        raise SystemExit(f'partition size {partition_size} is not a multiple of block size {block_size}')
    block_count = partition_size // block_size

    # cache_size must be a multiple of read/prog and divide block_size; 512 matches
    # the runtime mount (LfsPartition.cpp). lookahead just needs to be a multiple of 8.
    fs = LittleFS(
        block_size=block_size,
        block_count=block_count,
        read_size=read_size,
        prog_size=prog_size,
        cache_size=512,
        lookahead_size=128,
    )

    # Only www/ is packed, so stray files in the staging directory never end up in the image.
    file_count = 0
    for root, dirs, files in os.walk(os.path.join(staging_dir, 'www')):
        dirs.sort()
        rel_root = os.path.relpath(root, staging_dir).replace(os.sep, '/')
        fs.makedirs(rel_root, exist_ok=True)
        for name in sorted(files):
            rel_path = name if rel_root == '.' else f'{rel_root}/{name}'
            with open(os.path.join(root, name), 'rb') as f_in:
                with fs.open(rel_path, 'wb') as f_out:
                    f_out.write(f_in.read())
            file_count += 1

    os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
    with open(output, 'wb') as f:
        f.write(bytes(fs.context.buffer))

    log(f'Packed {file_count} files into {output} '
        f'({partition_size} bytes, {block_count} x {block_size}-byte blocks)')


def main():
    p = argparse.ArgumentParser(description='Build the OpenShock static filesystem image')
    p.add_argument('--frontend-dir', required=True, help='Path to the frontend/ project')
    p.add_argument('--staging-dir', required=True, help='Where to stage gzipped assets (contains www/)')
    p.add_argument('--output', required=True, help='Output littlefs image path')
    size = p.add_mutually_exclusive_group(required=True)
    size.add_argument('--partition-size', type=lambda v: int(v, 0), help='static0 partition size')
    size.add_argument('--partition-csv', help='partition table CSV to read the static0 size from')
    p.add_argument('--block-size', type=int, default=4096)
    p.add_argument('--read-size', type=int, default=128)
    p.add_argument('--prog-size', type=int, default=128)
    p.add_argument('--skip-frontend-build', action='store_true',
                   help='Reuse existing frontend/build without invoking pnpm')
    args = p.parse_args()

    if args.skip_frontend_build:
        build_dir = os.path.join(args.frontend_dir, 'build')
        if not os.path.isdir(build_dir):
            raise SystemExit(f'--skip-frontend-build set but {build_dir} does not exist')
    else:
        build_dir = build_frontend(args.frontend_dir)

    partition_size = args.partition_size
    if partition_size is None:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        from utils import partitions  # noqa: E402
        from pathlib import Path

        table = partitions.read_partitions(Path(args.partition_csv))
        if 'static0' not in table:
            raise SystemExit(f'{args.partition_csv} has no static0 partition')
        partition_size = table['static0']['size']

    stage_assets(build_dir, args.staging_dir)
    pack_image(args.staging_dir, args.output, partition_size,
               args.block_size, args.read_size, args.prog_size)


if __name__ == '__main__':
    sys.exit(main())
