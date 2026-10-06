"""Download the flatc compiler binaries (Linux + Windows) used by generate_schemas.py.

flatc must match the vendored flatbuffers runtime (the components/flatbuffers/flatbuffers submodule): generated code
static_asserts the exact flatbuffers version it was produced with. So by default this fetches the release whose
version the submodule's base.h declares.

  fetch_flatc.py                 fetch flatc matching the submodule
  fetch_flatc.py --upgrade       move the submodule to the latest flatbuffers release, then fetch its flatc
                                 (aliases: --update, --latest)

Each download is checked against the SHA-256 digest GitHub publishes for the release asset.
"""

import argparse
import hashlib
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import requests

ROOT = Path(__file__).resolve().parent.parent
SUBMODULE = ROOT / 'components' / 'flatbuffers' / 'flatbuffers'
BASE_H = SUBMODULE / 'include' / 'flatbuffers' / 'base.h'

RELEASES_API_URL = 'https://api.github.com/repos/google/flatbuffers/releases'
ASSET_MARKERS = ('Linux.flatc.binary.clang', 'Windows.flatc.binary')
TIMEOUT = 60


def submodule_version() -> str:
    """The runtime version the submodule declares, e.g. '25.12.19'."""
    text = BASE_H.read_text(encoding='utf-8')
    parts = []
    for name in ('MAJOR', 'MINOR', 'REVISION'):
        m = re.search(rf'#define\s+FLATBUFFERS_VERSION_{name}\s+(\d+)', text)
        if not m:
            raise SystemExit(f'error: FLATBUFFERS_VERSION_{name} not found in {BASE_H}')
        parts.append(m.group(1))
    return '.'.join(parts)


def release_for_version(version: str) -> dict:
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


def latest_release() -> dict:
    response = requests.get(f'{RELEASES_API_URL}/latest', timeout=TIMEOUT)
    response.raise_for_status()
    return response.json()


def git(*args: str) -> None:
    subprocess.run(['git', '-C', str(SUBMODULE), *args], check=True)


def bump_submodule(tag: str) -> None:
    print(f'Moving the flatbuffers submodule to {tag}')
    git('fetch', '--tags', 'origin')
    git('checkout', '--detach', tag)


def find_asset(release: dict, marker: str) -> dict:
    asset = next((a for a in release.get('assets', []) if marker in a['name']), None)
    if asset is None:
        raise SystemExit(f'error: no asset matching {marker!r} in flatbuffers release {release["tag_name"]}')
    return asset


def download_verified(asset: dict, dest_path: Path) -> None:
    response = requests.get(asset['browser_download_url'], stream=True, timeout=TIMEOUT)
    response.raise_for_status()

    sha256 = hashlib.sha256()
    with open(dest_path, 'wb') as f:
        for chunk in response.iter_content(chunk_size=8192):
            f.write(chunk)
            sha256.update(chunk)

    expected = (asset.get('digest') or '').removeprefix('sha256:')
    if not expected:
        raise SystemExit(f'error: GitHub published no digest for {asset["name"]}; refusing to use it unverified')
    if sha256.hexdigest() != expected:
        raise SystemExit(f'error: SHA-256 mismatch for {asset["name"]} (got {sha256.hexdigest()}, expected {expected})')

    print(f'Downloaded and verified: {asset["name"]}')


def fetch_binaries(release: dict, output_dir: Path) -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        for marker in ASSET_MARKERS:
            asset = find_asset(release, marker)
            zip_path = Path(temp_dir) / asset['name']
            download_verified(asset, zip_path)
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                zip_ref.extractall(output_dir)
            print(f'Extracted {asset["name"]} to {output_dir}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('output_dir', nargs='?', default=str(Path(__file__).resolve().parent),
                        help='directory to extract flatc into (default: scripts/)')
    parser.add_argument('--upgrade', '--update', '--latest', dest='upgrade', action='store_true',
                        help='move the flatbuffers submodule to the latest release and fetch its flatc')
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()
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
        sys.exit(f'error: download failed: {e}')
    except subprocess.CalledProcessError as e:
        sys.exit(f'error: git failed: {e}')

    if args.upgrade:
        print(f'\nflatbuffers is now at {release["tag_name"]}. Next: run scripts/generate_schemas.py so the generated code '
              'matches, then commit the submodule bump together with the regenerated sources.')


if __name__ == '__main__':
    main()
