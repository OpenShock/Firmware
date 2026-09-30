#!/usr/bin/env python3
"""Build the captive-portal static filesystem image (native ESP-IDF).

Pipeline:
  1. Build the SvelteKit frontend (pnpm), unless already built / running in CI.
  2. Gzip every built asset into <staging>/www/<path>.gz.
  3. Pack <staging>/ into a littlefs image sized for the `static0` partition.

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


def build_frontend(frontend_dir):
    """Run the SvelteKit production build unless it already exists or we're in CI.

    In CI the frontend is built once in a separate job and downloaded as an
    artifact into frontend/build, so we must not rebuild it here.
    """
    build_dir = os.path.join(frontend_dir, 'build')

    if os.environ.get('CI'):
        if not os.path.isdir(build_dir):
            raise SystemExit(f'CI build: expected prebuilt frontend at {build_dir}')
        log('CI detected — using prebuilt frontend/build')
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
    for root, _, files in os.walk(build_dir):
        rel = os.path.relpath(root, build_dir)
        dst_root = www_dir if rel == '.' else os.path.join(www_dir, rel)
        os.makedirs(dst_root, exist_ok=True)

        for name in files:
            src = os.path.join(root, name)
            if name.endswith('.gz'):
                dst = os.path.join(dst_root, name)
                shutil.copyfile(src, dst)
            else:
                dst = os.path.join(dst_root, name + '.gz')
                with open(src, 'rb') as f_in, gzip.open(dst, 'wb') as f_out:
                    f_out.write(f_in.read())
            count += 1

    log(f'Staged {count} gzipped assets into {www_dir}')
    if not os.path.exists(os.path.join(www_dir, 'index.html.gz')):
        raise SystemExit(f'{www_dir}/index.html.gz missing — frontend build produced no index.html')
    return www_dir


def pack_image(staging_dir, output, partition_size, block_size, read_size, prog_size):
    """Pack staging_dir into a littlefs image of exactly partition_size bytes."""
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

    file_count = 0
    for root, _, files in os.walk(staging_dir):
        rel_root = os.path.relpath(root, staging_dir).replace(os.sep, '/')
        if rel_root != '.':
            fs.makedirs(rel_root, exist_ok=True)
        for name in files:
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
    p.add_argument('--partition-size', required=True, type=lambda v: int(v, 0), help='static0 partition size')
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

    stage_assets(build_dir, args.staging_dir)
    pack_image(args.staging_dir, args.output, args.partition_size,
               args.block_size, args.read_size, args.prog_size)


if __name__ == '__main__':
    sys.exit(main())
