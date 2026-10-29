"""Download the flatc compiler binaries (Linux + Windows) used by generate_schemas.py.

flatc must match the vendored flatbuffers runtime (the components/flatbuffers/flatbuffers submodule): generated code
static_asserts the exact flatbuffers version it was produced with. So by default this fetches the release whose
version the submodule's base.h declares.

  fetch_flatc.py                 fetch flatc matching the submodule
  fetch_flatc.py --upgrade       move the submodule to the latest flatbuffers release, then fetch its flatc

Each download is checked against the SHA-256 digest GitHub publishes for the release asset.
"""

import argparse
import hashlib
import os
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path
from typing import Any

import requests

from utils.paths import ROOT

SUBMODULE = ROOT / 'components' / 'flatbuffers' / 'flatbuffers'
BASE_H = SUBMODULE / 'include' / 'flatbuffers' / 'base.h'
SCRIPTS_DIR = Path(__file__).resolve().parent

RELEASES_API_URL = 'https://api.github.com/repos/google/flatbuffers/releases'
ASSET_MARKERS = ('Linux.flatc.binary.clang', 'Windows.flatc.binary')
TIMEOUT = 60

# A GitHub release, or one of its assets, as the REST API returns it.
Release = dict[str, Any]


def submodule_version() -> str:
    """The runtime version the submodule declares, e.g. '25.12.19'."""
    if not BASE_H.is_file():
        raise SystemExit(
            f'error: {BASE_H} not found; check out the submodule first: '
            'git submodule update --init components/flatbuffers/flatbuffers'
        )
    text = BASE_H.read_text(encoding='utf-8')
    parts = []
    for name in ('MAJOR', 'MINOR', 'REVISION'):
        m = re.search(rf'#define\s+FLATBUFFERS_VERSION_{name}\s+(\d+)', text)
        if not m:
            raise SystemExit(f'error: FLATBUFFERS_VERSION_{name} not found in {BASE_H}')
        parts.append(m.group(1))
    return '.'.join(parts)


def release_for_version(version: str) -> Release:
    """The GitHub release for `version`. Tags look like 'v25.12.19' or 'v25.12.19-2026-02-06-03fffb2'."""
    tag_pattern = re.compile(rf'^v{re.escape(version)}($|-)')
    page = 1
    while True:
        response = requests.get(RELEASES_API_URL, params={'per_page': 100, 'page': page}, timeout=TIMEOUT)
        response.raise_for_status()
        releases = response.json()
        if not releases:
            raise SystemExit(f'error: no flatbuffers release found for version {version}')
        for release in releases:
            if tag_pattern.match(release['tag_name']):
                return release
        page += 1


def latest_release() -> Release:
    response = requests.get(f'{RELEASES_API_URL}/latest', timeout=TIMEOUT)
    response.raise_for_status()
    return response.json()


def git(*args: str) -> None:
    subprocess.run(['git', '-C', str(SUBMODULE), *args], check=True)


def bump_submodule(tag: str) -> None:
    print(f'Moving the flatbuffers submodule to {tag}')
    git('fetch', '--tags', 'origin')
    git('checkout', '--detach', tag)


def find_asset(release: Release, marker: str) -> Release:
    asset = next((a for a in release.get('assets', []) if marker in a['name']), None)
    if asset is None:
        raise SystemExit(f'error: no asset matching {marker!r} in flatbuffers release {release["tag_name"]}')
    return asset


def download_verified(asset: Release, dest_path: Path) -> None:
    """Download an asset, refusing it unless it matches the SHA-256 digest GitHub published for it."""
    expected = (asset.get('digest') or '').removeprefix('sha256:')
    if not expected:
        raise SystemExit(f'error: GitHub published no digest for {asset["name"]}; refusing to use it unverified')

    sha256 = hashlib.sha256()
    with requests.get(asset['browser_download_url'], stream=True, timeout=TIMEOUT) as response:
        response.raise_for_status()
        with dest_path.open('wb') as f:
            for chunk in response.iter_content(chunk_size=8192):
                f.write(chunk)
                sha256.update(chunk)

    if sha256.hexdigest() != expected:
        raise SystemExit(f'error: SHA-256 mismatch for {asset["name"]} (got {sha256.hexdigest()}, expected {expected})')
    print(f'Downloaded and verified: {asset["name"]}')


def make_executable(path: Path) -> None:
    """Restore the executable bit, which zipfile drops on extraction; a no-op on Windows."""
    if os.name == 'posix' and path.is_file():
        path.chmod(path.stat().st_mode | 0o111)


def fetch_binaries(release: Release, output_dir: Path) -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        for marker in ASSET_MARKERS:
            asset = find_asset(release, marker)
            zip_path = Path(temp_dir) / asset['name']
            download_verified(asset, zip_path)
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                zip_ref.extractall(output_dir)
            print(f'Extracted {asset["name"]} to {output_dir}')
    make_executable(output_dir / 'flatc')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        'output_dir',
        nargs='?',
        type=Path,
        default=SCRIPTS_DIR,
        help='directory to extract flatc into (default: scripts/)',
    )
    parser.add_argument(
        '--upgrade',
        action='store_true',
        help='move the flatbuffers submodule to the latest release and fetch its flatc',
    )
    args = parser.parse_args(argv)

    output_dir: Path = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        if args.upgrade:
            release = latest_release()
            bump_submodule(release['tag_name'])
        else:
            version = submodule_version()
            print(f'flatbuffers submodule is at {version}')
            release = release_for_version(version)

        fetch_binaries(release, output_dir)
    except requests.RequestException as e:
        raise SystemExit(f'error: download failed: {e}') from None
    except subprocess.CalledProcessError as e:
        raise SystemExit(f'error: git failed: {e}') from None
    except OSError as e:
        raise SystemExit(f'error: {e}') from None

    if args.upgrade:
        print(
            f'\nflatbuffers is now at {release["tag_name"]}. Next: run scripts/generate_schemas.py so the generated '
            'code matches, then commit the submodule bump together with the regenerated sources.'
        )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
