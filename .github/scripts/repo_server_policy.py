# !/usr/bin/env python3
"""Refuse a run that may not publish the version and channel get-vars resolved.

get-vars settles what this run is before anything builds, so nothing here overrides it - a build that reports one version must not be published as another.

These are the repository's rules, not the server's. The server rejects an unregistered repo, a bad hash or an unknown board on its own, and checking those here would only give them a second place to be wrong. What it cannot know is that a stable release descends from master, or that a maintainer staged a draft for it, because those are facts about this repository.

Everything here applies to prod only. A dev publish reaches no device, so constraining it buys nothing and costs the ability to test.
"""

import time

import requests

from gha import env, fail, notice, require_env

API = 'https://api.github.com'

# Pinned rather than left to the default, so a new default cannot change what these checks see.
# GitHub names its supported versions in the 400 it returns for an unknown one, which is how to find the next.
API_VERSION = '2026-03-10'
# The branch each channel is cut from.
# develop is absent because the nightly is already the only thing publishing it, and a tag is not required to descend from it.
CHANNEL_BRANCH = {'stable': 'master', 'beta': 'beta'}

DRAFT_ATTEMPTS = 12
DRAFT_INTERVAL = 5


def get(path: str, *, allow_404: bool = False) -> dict | None:
    """One GET against the API, or None when allow_404 and the thing is not there."""
    try:
        resp = requests.get(
            f'{API}{path}',
            headers={
                'Authorization': f"Bearer {env('GITHUB_TOKEN')}",
                'Accept': 'application/vnd.github+json',
                'X-GitHub-Api-Version': API_VERSION,
            },
            timeout=(10, 30),
        )
    except requests.RequestException as err:
        fail(f'Could not reach the GitHub API for {path}: {err}')

    if resp.status_code == 404 and allow_404:
        return None
    if resp.status_code != 200:
        fail(f'GitHub returned HTTP {resp.status_code} for {path}: {(resp.text or "").strip()[:300]}')
    return resp.json()


def require_branch_contains(repo: str, channel: str, sha: str) -> None:
    """A prod publish onto beta or stable has to come from the branch that channel is cut from.

    A tag is not a branch, so the test is containment rather than equality: compare reports head relative to base, so an ancestor of the branch reads as behind or identical.
    Without this, tagging any commit would put arbitrary work on a channel the fleet follows.
    """
    branch = CHANNEL_BRANCH[channel]
    status = get(f'/repos/{repo}/compare/{branch}...{sha}')['status']
    if status not in ('identical', 'behind'):
        fail(f'{sha} is not contained in {branch} (compare says {status}), so it cannot publish to the {channel} channel on prod.')
    print(f'{sha} is contained in {branch}.')


def require_staged_draft(repo: str, version: str) -> None:
    """release.yml stages a draft for the version before it may go live.

    Otherwise a stray tag could put firmware on a channel devices follow.
    A draft is not reachable by tag name, since GitHub only associates a release with its tag once published, so this reads the list.
    release.yml pushes the tag that triggers this run and only then stages the draft, so poll rather than failing on the window between the two.
    """
    for attempt in range(1, DRAFT_ATTEMPTS + 1):
        releases = get(f'/repos/{repo}/releases?per_page=100') or []
        match = next((r for r in releases if r.get('tag_name') == version), None)
        if match and match.get('draft'):
            print(f'Draft release for {version} is staged.')
            return
        state = 'published' if match else 'missing'
        print(f'Draft for {version} not staged yet (state: {state}); retry {attempt}/{DRAFT_ATTEMPTS}...', flush=True)
        time.sleep(DRAFT_INTERVAL)

    fail(f'No draft release for {version} after ~{DRAFT_ATTEMPTS * DRAFT_INTERVAL}s. release.yml must stage the draft before the publish runs.')


def main() -> int:
    require_env('SERVER', 'REPO', 'SHA', 'VERSION', 'CHANNEL', 'GITHUB_TOKEN')

    server = env('SERVER')
    repo = env('REPO')
    sha = env('SHA')
    version = env('VERSION')
    channel = env('CHANNEL')

    print(f'Publishing {version} to the {channel} channel on {server}.')

    if server != 'prod':
        notice('Publishing to dev, so none of the production rules apply.')
        return 0

    if channel in CHANNEL_BRANCH:
        require_branch_contains(repo, channel, sha)
        require_staged_draft(repo, version)

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
