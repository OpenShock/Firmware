# !/usr/bin/env python3
"""Drive one phase of a firmware release on the repository server.

CI's whole contract: submit each blob with the hash it computed and believe the server about everything else.

`MODE` picks the phase, because the phases run in different jobs.
`init` opens the release before anything is built, so the server learns a release is coming while the build is still starting and refuses it now rather than half an hour from now.
`publish` and `dry-run` upload every board and then promote or discard; `abort` discards a release the build never got to.

The staged release is a resource whose lifetime now outlives this process: init hands the id to the workflow, and ci-build's abort job is the `finally` that closes it.
"""

import hashlib
import json
import time
from contextlib import ExitStack
from datetime import datetime, timezone
from pathlib import Path

import requests

from gha import env, fail, require_env, set_output, warn

VALID_MODES = ('init', 'publish', 'dry-run', 'abort')
# The server parses this into an enum; anything else is a 400 several steps later, after artifacts have been downloaded.
# Branch builds derive a channel from the branch name, so this catches a feature branch being published by accident.
VALID_CHANNELS = ('stable', 'beta', 'develop')

# Server field name -> the artifact it carries.
# app and staticfs are what an OTA update needs; the rest are for the flashtool, which writes a whole chip rather than updating one.
# The manifest keys and the file field names come from this one table, so they cannot drift apart.
BUILD_ARTIFACTS = {
    'app': 'app.bin',
    'bootloader': 'bootloader.bin',
    'partitions': 'partition-table.bin',
}
STATICFS_FIELD = 'staticfs'
MERGED_FIELD = 'merged'
MERGED_GLOB = 'OpenShock_*.bin'

UPLOAD_RETRIES = 3
UPLOAD_BACKOFF_SECONDS = 5
CHUNK = 1 << 20


def session_for(token: str) -> requests.Session:
    """One session for the whole run, so the bearer token is attached in a single place and the connection to the server is reused across every board.
    """
    s = requests.Session()
    s.headers.update({'Authorization': f'Bearer {token}', 'Accept': 'application/json'})
    return s


def show_response(resp: requests.Response) -> None:
    """Print the server's problem details.
    It names its own causes better than any guess made from the status code alone, so the body is worth the log lines.
    """
    text = (resp.text or '').strip()
    if text:
        print(text, flush=True)


def extract_changelog(path: Path) -> str:
    """The newest `# ` section, which is the release being built on a tag and the previous release on a branch.
    Either way it is a real changelog going through the real parser, so a grammar the server rejects surfaces on an ordinary build.
    """
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


def resolve_artifacts(artifacts: Path, board: str) -> dict[str, Path]:
    """Every artifact the server wants for one board, or a hard failure naming the first one that is missing.
    """
    build = artifacts / f'firmware_build_{board}'
    found: dict[str, Path] = {}

    for field, filename in BUILD_ARTIFACTS.items():
        found[field] = build / filename
    found[STATICFS_FIELD] = artifacts / 'firmware_staticfs' / 'staticfs.bin'

    merged = sorted((artifacts / f'firmware_merged_{board}').glob(MERGED_GLOB))
    if merged:
        found[MERGED_FIELD] = merged[0]

    for field in (*BUILD_ARTIFACTS, STATICFS_FIELD, MERGED_FIELD):
        path = found.get(field)
        if path is None or not path.is_file():
            expected = path if path else f'{artifacts}/firmware_merged_{board}/{MERGED_GLOB}'
            fail(f'{board} is missing its {field} artifact (expected {expected})')
    return found


def upload_board(session: requests.Session, server: str, release_id: str, board: str, artifacts: Path) -> None:
    """One request per board, carrying that board's artifacts and a sha256 manifest the server re-computes before writing anything.

    Retried on transport errors and 5xx: the request is a whole-board PUT, so a repeat replaces the same artifacts rather than appending anything.
    init and publish below are deliberately not retried - they are POSTs whose effect a timeout leaves unknown.
    The file handles are reopened per attempt, since a retry has to send the body from the start and the previous attempt consumed the streams.
    """
    paths = resolve_artifacts(artifacts, board)
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

        print(f'::error::Upload failed for {board} (HTTP {resp.status_code})', flush=True)
        show_response(resp)
        raise SystemExit(1)


def init_release(session: requests.Session, server: str, *, version: str, channel: str, boards: list[str], changelog: str, nofail: bool) -> str:
    """Stage the release.
    There is no preflight: an unregistered repository, a missing scope and an unknown board are all non-success responses from here, before a single artifact is uploaded, and each names its own cause.
    """
    resp = session.post(
        f'{server}/2/firmware/releases' + ('?nofail' if nofail else ''),
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
        print(f'::error::Init failed (HTTP {resp.status_code})', flush=True)
        show_response(resp)
        raise SystemExit(1)

    data = resp.json()
    release_id = data['id']
    print(f"Release {release_id} staged as {data.get('status')} for {version} on {channel}", flush=True)
    return release_id


def promote(session: requests.Session, server: str, release_id: str) -> None:
    resp = session.post(
        f'{server}/2/firmware/releases/{release_id}/publish',
        json={},
        timeout=(30, 120),
    )
    if resp.status_code != 201:
        print(f'::error::Publish failed (HTTP {resp.status_code})', flush=True)
        show_response(resp)
        raise SystemExit(1)
    print(f'Published {release_id}', flush=True)


def discard(session: requests.Session, server: str, release_id: str, mode: str) -> None:
    """A staged release that is never aborted holds its version against a later attempt (init returns ReleaseAlreadyStaging) until the server's TTL job reaps it, so a failed run that left one behind would block the retry that was meant to fix it.

    Never raises: this runs from a finally, where the failure being cleaned up after is the one worth reporting.
    """
    try:
        resp = session.delete(f'{server}/2/firmware/releases/{release_id}', timeout=(30, 60))
    except requests.RequestException as err:
        warn(f'Could not reach the server to discard release {release_id} ({err}). It will be reaped by the server TTL job.')
        return

    if resp.status_code == 204:
        if mode == 'dry-run':
            print('Dry run complete: release staged, verified and discarded. Nothing was published.', flush=True)
        else:
            print('The run failed; the staged release was discarded so a retry can reuse this version.', flush=True)
        return

    # The workflow's abort job is a backstop for a publish that never ran, so it also fires after one that ran and cleaned up after itself.
    # Finding the release already gone is that job succeeding, not failing.
    if mode == 'abort' and resp.status_code in (404, 409):
        print(f'Release {release_id} was already closed; nothing to abort.', flush=True)
        return
    # Not fatal on its own - the TTL job reaps abandoned releases - but it means the next attempt at this version will be refused until that happens, so say so loudly.
    warn(f'Could not discard release {release_id} (HTTP {resp.status_code}). It will be reaped by the server TTL job.')
    show_response(resp)


def read_boards() -> list[str]:
    return [b.strip() for b in env('BOARDS').splitlines() if b.strip()]


def do_init(session: requests.Session, server: str) -> int:
    """Open the release and hand its id back, leaving it staged on purpose.

    Nothing is uploaded here, so every check the server makes at init - semver, channel, a version already published, a concurrent release holding the same one, an unknown board - lands before the build burns its half hour.
    """
    require_env('VERSION', 'CHANNEL')
    version = env('VERSION')
    channel = env('CHANNEL')
    boards = read_boards()

    if channel not in VALID_CHANNELS:
        fail(f"CHANNEL must be one of {'/'.join(VALID_CHANNELS)}, got '{channel}'")
    if not boards:
        fail('BOARDS is empty; there is nothing to declare.')

    release_id = init_release(
        session,
        server,
        version=version,
        channel=channel,
        boards=boards,
        changelog=extract_changelog(Path(env('CHANGELOG_FILE', 'CHANGELOG.md'))),
        nofail=env('NOFAIL').lower() == 'true',
    )
    set_output('release-id', release_id)
    return 0


def do_upload(session: requests.Session, server: str, mode: str) -> int:
    """Upload every board into the release init opened, then promote or discard it."""
    require_env('RELEASE_ID')
    release_id = env('RELEASE_ID')
    boards = read_boards()
    artifacts = Path(env('ARTIFACTS_DIR', 'artifacts'))

    if not boards:
        fail('BOARDS is empty; there is nothing to upload.')

    published = False
    try:
        for board in boards:
            upload_board(session, server, release_id, board, artifacts)
        if mode == 'publish':
            promote(session, server, release_id)
            published = True
    finally:
        if not published:
            discard(session, server, release_id, mode)

    return 0


def do_abort(session: requests.Session, server: str) -> int:
    """Close a release whose build never reached the upload.

    Runs from the workflow's always() job, so it reports rather than fails: the build failure it is cleaning up after is the one worth surfacing.
    """
    release_id = env('RELEASE_ID')
    if not release_id:
        print('No release was staged, so there is nothing to abort.', flush=True)
        return 0

    discard(session, server, release_id, 'abort')
    return 0


def main() -> int:
    require_env('SERVER_URL', 'TOKEN')
    server = env('SERVER_URL').rstrip('/')
    mode = env('MODE', 'dry-run')

    if mode not in VALID_MODES:
        fail(f"MODE must be one of {'/'.join(VALID_MODES)}, got '{mode}'")

    session = session_for(env('TOKEN'))

    if mode == 'init':
        return do_init(session, server)
    if mode == 'abort':
        return do_abort(session, server)
    return do_upload(session, server, mode)


if __name__ == '__main__':
    raise SystemExit(main())
