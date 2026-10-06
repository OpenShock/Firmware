"""Download the flatc compiler binaries (Linux + Windows) used by generate_schemas.py.

The release is pinned: generated code static_asserts the exact flatbuffers version it was produced with
(components/serialization/include/serialization/_fbs/*_generated.h), so flatc must match the vendored runtime.
Each download is checked against the SHA-256 digest GitHub publishes for the release asset.
"""

import argparse
import hashlib
import sys
import tempfile
import zipfile
from pathlib import Path

import requests

# Keep in sync with the flatbuffers runtime in components/flatbuffers.
FLATBUFFERS_VERSION = 'v25.12.19'
RELEASE_API_URL = f'https://api.github.com/repos/google/flatbuffers/releases/tags/{FLATBUFFERS_VERSION}'
ASSET_MARKERS = ('Linux.flatc.binary.clang', 'Windows.flatc.binary')
TIMEOUT = 60


def find_asset(assets, marker):
    asset = next((a for a in assets if marker in a['name']), None)
    if asset is None:
        raise SystemExit(f'error: no asset matching {marker!r} in flatbuffers {FLATBUFFERS_VERSION}')
    return asset


def download_verified(asset, dest_path: Path):
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


def fetch_flatc(output_dir: Path):
    response = requests.get(RELEASE_API_URL, timeout=TIMEOUT)
    response.raise_for_status()
    assets = response.json().get('assets', [])

    with tempfile.TemporaryDirectory() as temp_dir:
        for marker in ASSET_MARKERS:
            asset = find_asset(assets, marker)
            zip_path = Path(temp_dir) / asset['name']
            download_verified(asset, zip_path)
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                zip_ref.extractall(output_dir)
            print(f'Extracted {asset["name"]} to {output_dir}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_dir', nargs='?', default=str(Path(__file__).resolve().parent),
                        help='directory to extract flatc into (default: scripts/)')
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        fetch_flatc(output_dir)
    except requests.RequestException as e:
        sys.exit(f'error: download failed: {e}')


if __name__ == '__main__':
    main()
