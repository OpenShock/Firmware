#!/usr/bin/env python3
"""Drive one phase of a firmware release on the repository server, picked by `MODE`.

`init` stages the release before the build, `publish` uploads every board and promotes it, `abort` discards a release the build never got to.
"""

import hashlib
import json
import time
from contextlib import ExitStack
from datetime import datetime, timezone
from pathlib import Path
from typing import NoReturn

import requests

from utils.boards import parse_board_list
from utils.channels import CHANNELS
from utils.gha import env, error, fail, require_env, set_output, warn

VALID_MODES = ('init', 'publish', 'abort')

# Server field name -> the artifact it carries.
# app and staticfs are what an OTA update needs; the rest are for the flashtool, which writes a whole chip rather than updating one.
# The manifest keys and the file field names come from this one table, so they cannot drift apart.
BUILD_ARTIFACTS = {
    'app': 'app.bin',
    'bootloader': 'bootloader.bin',
    'partitions': 'partition-table.bin',
}
STATICFS_FIELD = 'staticfs'
# What the staticfs field carries: a 1.x-numbered release (the 1.9.x pivot) is installed by 1.x firmware, whose OTA
# flashes staticfs into its littlefs static0 partition and checks it by mounting it, so it gets the littlefs image;
# every later release gets the flat www image. The build-staticfs action builds both.
STATICFS_FILE = 'www.bin'
STATICFS_FILE_1X = 'staticfs.bin'
MERGED_FIELD = 'merged'


def staticfs_file_for(version: str) -> str:
    return STATICFS_FILE_1X if version.split('.', 1)[0] == '1' else STATICFS_FILE


UPLOAD_RETRIES = 3
UPLOAD_BACKOFF_SECONDS = 5
CHUNK = 1 << 20


def session_for(token: str) -> requests.Session:
    """One session per run: attaches the bearer token once and reuses the connection."""
    s = requests.Session()
    s.headers.update({'Authorization': f'Bearer {token}', 'Accept': 'application/json'})
    return s


def show_response(resp: requests.Response) -> None:
    """Print the server's problem details; they name the cause better than the status code."""
    text = (resp.text or '').strip()
    if text:
        print(text, flush=True)


def die(message: str, resp: requests.Response) -> NoReturn:
    error(message)
    show_response(resp)
    raise SystemExit(1)


def extract_changelog(path: Path) -> str:
    """The newest `# ` section: the release being built on a tag, the previous release on a branch."""
    if not path.is_file():
        fail(f"Changelog file '{path}' not found.")
    lines = path.read_text(encoding='utf-8').splitlines()
    section: list[str] = []
    for line in lines:
        if line.startswith('# '):
            if section:
                break
            section.append(line)
        elif section:
            section.append(line)
    text = '\n'.join(section).strip()
    if not text:
        fail(f"No '# ' heading found in '{path}'; init would reject an empty changelog.")
    print(f'Changelog section: {section[0]} ({len(section)} lines)', flush=True)
    return text


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as fh:
        while chunk := fh.read(CHUNK):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_artifacts(artifacts: Path, board: str, version: str) -> dict[str, Path]:
    """Every artifact the server wants for one board, or a failure naming the first one missing."""
    build = artifacts / 'firmware_boards' / board
    # The package job merges every board at once, so the merged binaries share a directory and are told apart by the board in their filename.
    merged_glob = f'OpenShock_{board}_[0-9]*.bin'
    merged = sorted((artifacts / 'firmware_merged').glob(merged_glob))
    if not merged:
        fail(f'{board} is missing its {MERGED_FIELD} artifact (expected {artifacts}/firmware_merged/{merged_glob})')

    found = {field: build / filename for field, filename in BUILD_ARTIFACTS.items()}
    found[STATICFS_FIELD] = artifacts / 'firmware_staticfs' / staticfs_file_for(version)
    found[MERGED_FIELD] = merged[0]
    for field, path in found.items():
        if not path.is_file():
            fail(f'{board} is missing its {field} artifact (expected {path})')
    return found


def upload_board(
    session: requests.Session, server: str, release_id: str, board: str, artifacts: Path, version: str
) -> None:
    """PUT one board's artifacts with a sha256 manifest.

    Retried on transport errors and 5xx, since a repeated PUT replaces rather than appends; init and publish are POSTs and are not retried.
    """
    paths = resolve_artifacts(artifacts, board, version)
    manifest = {field: sha256_of(path) for field, path in paths.items()}

    url = f'{server}/2/firmware/releases/{release_id}/boards/{board}'
    for attempt in range(1, UPLOAD_RETRIES + 1):
        try:
            with ExitStack() as stack:
                files = {
                    field: (path.name, stack.enter_context(path.open('rb')), 'application/octet-stream')
                    for field, path in paths.items()
                }
                resp = session.put(
                    url,
                    files=files,
                    data={'sha256': json.dumps(manifest)},
                    timeout=(30, 300),
                )
        except requests.RequestException as err:
            if attempt == UPLOAD_RETRIES:
                fail(f'Upload failed for {board} after {attempt} attempts: {err}')
            warn(f'Upload for {board} failed ({err}); retrying {attempt}/{UPLOAD_RETRIES - 1}...')
            time.sleep(UPLOAD_BACKOFF_SECONDS)
            continue

        if resp.status_code == 200:
            print(f'  {board}: {len(resp.json())} artifacts accepted', flush=True)
            return

        if resp.status_code >= 500 and attempt < UPLOAD_RETRIES:
            warn(f'Upload for {board} returned HTTP {resp.status_code}; retrying {attempt}/{UPLOAD_RETRIES - 1}...')
            show_response(resp)
            time.sleep(UPLOAD_BACKOFF_SECONDS)
            continue

        die(f'Upload failed for {board} (HTTP {resp.status_code})', resp)


def init_release(
    session: requests.Session, server: str, *, version: str, channel: str, boards: list[str], changelog: str
) -> str:
    """Stage the release; the server rejects a bad repository, scope or board here, before any upload."""
    resp = session.post(
        f'{server}/2/firmware/releases',
        json={
            'version': version,
            'channel': channel,
            'releaseDate': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
            'boards': boards,
            'changelog': changelog,
        },
        timeout=(30, 60),
    )
    if resp.status_code != 201:
        die(f'Init failed (HTTP {resp.status_code})', resp)

    data = resp.json()
    release_id = data['id']
    print(f'Release {release_id} staged as {data.get("status")} for {version} on {channel}', flush=True)
    return release_id


class PublishOutcomeUnknown(Exception):
    """The publish request was sent but no answer came back, so the release may or may not be live."""


def promote(session: requests.Session, server: str, release_id: str) -> None:
    try:
        resp = session.post(
            f'{server}/2/firmware/releases/{release_id}/publish',
            json={},
            timeout=(30, 120),
        )
    except requests.RequestException as e:
        error(f'Publish request for {release_id} got no response ({e}); the release may already be live')
        raise PublishOutcomeUnknown(release_id) from e
    if resp.status_code != 201:
        die(f'Publish failed (HTTP {resp.status_code})', resp)
    print(f'Published {release_id}', flush=True)


def discard(session: requests.Session, server: str, release_id: str) -> str:
    """Delete a staged release, which otherwise blocks a retry of its version until the server's TTL job reaps it.

    Returns 'discarded', 'already_closed' or 'failed' (warned about here). Never raises: the failure being cleaned up after is the one worth reporting.
    """
    try:
        resp = session.delete(f'{server}/2/firmware/releases/{release_id}', timeout=(30, 60))
    except requests.RequestException as err:
        warn(
            f'Could not reach the server to discard release {release_id} ({err}). It will be reaped by the server TTL job.'
        )
        return 'failed'

    if resp.status_code == 204:
        return 'discarded'
    # The abort job also fires after a publish that already cleaned up, so a release that is gone is success.
    if resp.status_code in (404, 409):
        return 'already_closed'
    warn(f'Could not discard release {release_id} (HTTP {resp.status_code}). It will be reaped by the server TTL job.')
    show_response(resp)
    return 'failed'


def report_discard(status: str, release_id: str) -> None:
    if status == 'discarded':
        print('The run failed; the staged release was discarded so a retry can reuse this version.', flush=True)
    elif status == 'already_closed':
        print(f'Release {release_id} was already closed; nothing to discard.', flush=True)


def read_boards() -> list[str]:
    return parse_board_list(env('BOARDS'))


def do_init(session: requests.Session, server: str) -> int:
    """Open the release and leave it staged, so the server's checks fail before the build runs."""
    require_env('VERSION', 'CHANNEL')
    version = env('VERSION')
    channel = env('CHANNEL')
    boards = read_boards()

    if channel not in CHANNELS:
        fail(f"CHANNEL must be one of {'/'.join(CHANNELS)}, got '{channel}'")
    if not boards:
        fail('BOARDS is empty; there is nothing to declare.')

    release_id = init_release(
        session,
        server,
        version=version,
        channel=channel,
        boards=boards,
        changelog=extract_changelog(Path('CHANGELOG.md')),
    )
    set_output('release-id', release_id)
    return 0


def do_publish(session: requests.Session, server: str) -> int:
    """Upload every board into the release init opened, then promote it; discard it on any failure."""
    require_env('RELEASE_ID', 'VERSION')
    release_id = env('RELEASE_ID')
    version = env('VERSION')
    boards = read_boards()
    artifacts = Path('artifacts')

    if not boards:
        fail('BOARDS is empty; there is nothing to upload.')

    try:
        for board in boards:
            upload_board(session, server, release_id, board, artifacts, version)
        promote(session, server, release_id)
    except PublishOutcomeUnknown:
        # Discarding now could delete a release that did go live; leave it for a human to check.
        set_output('outcome-unknown', 'true')
        fail(f'Not discarding {release_id}: verify on the repository server whether it was published.')
    except BaseException:
        report_discard(discard(session, server, release_id), release_id)
        raise

    return 0


def do_abort(session: requests.Session, server: str) -> int:
    """Close a release whose build never reached the upload; reports rather than fails."""
    release_id = env('RELEASE_ID')
    if not release_id:
        print('No release was staged, so there is nothing to abort.', flush=True)
        return 0

    report_discard(discard(session, server, release_id), release_id)
    return 0


def main() -> int:
    require_env('SERVER_URL', 'TOKEN')
    server = env('SERVER_URL').rstrip('/')
    mode = env('MODE')

    if mode not in VALID_MODES:
        fail(f"MODE must be one of {'/'.join(VALID_MODES)}, got '{mode}'")

    session = session_for(env('TOKEN'))

    if mode == 'init':
        return do_init(session, server)
    if mode == 'abort':
        return do_abort(session, server)
    return do_publish(session, server)


if __name__ == '__main__':
    raise SystemExit(main())
