#!/usr/bin/env python3
"""Build the captive-portal asset image (www.bin) for the `www` partition.

Pipeline:
  1. Build the SvelteKit frontend (pnpm) when its sources are newer than the last build (unless --skip-frontend-build, which CI passes).
  2. Gzip every built asset (a prebuilt <name>.gz is taken as-is).
  3. Pack them into one flat read-only image (scripts/utils/wwwimage.py), checked to fit the smallest `www` partition.

The output is reproducible: gzip headers carry no timestamp and entries are sorted, so an unchanged frontend gives a
byte-identical www.bin (and the same hash, so devices don't re-flash it). One image serves every board: it is not
padded to any partition size, only checked to fit.

Invoked by CMake (see the root CMakeLists.txt), and by CI with --skip-frontend-build.
"""

import argparse
import gzip
import os
import shutil
import subprocess
from collections.abc import Iterable
from pathlib import Path

from utils import partitions, wwwimage

WWW_PARTITION = 'www'

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

# Assets with a content hash in their name never change under the same path, so browsers may cache them for long.
IMMUTABLE_PREFIXES = ('_app/immutable/',)


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
    # mtime=0 and no filename: the gzip bytes depend only on the content
    import io

    out = io.BytesIO()
    with gzip.GzipFile(filename='', mode='wb', fileobj=out, mtime=0) as gz:
        gz.write(raw)
    return out.getvalue()


def collect_assets(build_dir: Path) -> list[wwwimage.Entry]:
    """Every file under build_dir as a gzipped entry at its relative path (a prebuilt <name>.gz wins over <name>)."""
    files: dict[str, Path] = {}
    for root, dirs, names in os.walk(build_dir):
        dirs.sort()
        for name in sorted(names):
            src = Path(root) / name
            files[src.relative_to(build_dir).as_posix()] = src

    entries = []
    for rel, src in files.items():
        if rel.endswith('.gz'):
            continue
        prebuilt = files.get(rel + '.gz')
        data = prebuilt.read_bytes() if prebuilt is not None else gzip_bytes(src.read_bytes())
        entries.append(wwwimage.Entry(rel, data, gzip=True, immutable=rel.startswith(IMMUTABLE_PREFIXES)))
    # A .gz with no raw sibling is an asset of its own, served under the name without .gz
    for rel, src in files.items():
        if rel.endswith('.gz') and rel[: -len('.gz')] not in files:
            entries.append(
                wwwimage.Entry(
                    rel[: -len('.gz')], src.read_bytes(), gzip=True, immutable=rel.startswith(IMMUTABLE_PREFIXES)
                )
            )

    if not any(e.path == 'index.html' for e in entries):
        raise SystemExit(f'error: {build_dir}/index.html missing: the frontend build produced no index.html')
    return entries


def www_partition_size(csv_path: Path) -> int:
    table = partitions.read_partitions(csv_path)
    if WWW_PARTITION not in table:
        raise SystemExit(f'error: {csv_path} has no {WWW_PARTITION} partition')
    return table[WWW_PARTITION]['size']


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
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
