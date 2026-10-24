#!/usr/bin/env python3
"""Refuse a run that may not publish the version and channel get_vars resolved.

These are the repository's rules, not the server's: that a stable release descends from master and that a maintainer staged a draft for it.
They apply to prod only; a dev publish reaches no device.
"""

import time
from typing import Any

import requests

from utils.gha import env, fail, notice, require_env
from utils.github_api import GitHubError, github_get

# The channels the repository server's enum knows. A feature branch derives its own name, which is not one of them.
CHANNELS = ('stable', 'beta', 'develop')
# The branch a commit must be contained in to publish to the channel on prod.
CHANNEL_BRANCH = {'stable': 'master', 'beta': 'beta', 'develop': 'develop'}
# Channels whose prod publishes also need a staged draft release (develop builds have no release).
DRAFT_CHANNELS = ('stable', 'beta')

DRAFT_ATTEMPTS = 12
DRAFT_INTERVAL = 5


def get(path: str) -> Any:
    try:
        return github_get(path, env('GITHUB_TOKEN'))
    except requests.RequestException as err:
        fail(f'Could not reach the GitHub API for {path}: {err}')
    except GitHubError as err:
        fail(f'GitHub returned {err} for {path}')


def require_branch_contains(repo: str, channel: str, sha: str) -> None:
    """A prod publish onto beta or stable has to come from the branch that channel is cut from.

    A tag is not a branch, so the test is containment: compare reports an ancestor of the branch as behind or identical.
    """
    branch = CHANNEL_BRANCH[channel]
    status = get(f'/repos/{repo}/compare/{branch}...{sha}')['status']
    if status not in ('identical', 'behind'):
        fail(f'{sha} is not contained in {branch} (compare says {status}), so it cannot publish to the {channel} channel on prod.')
    print(f'{sha} is contained in {branch}.')


def require_staged_draft(repo: str, version: str) -> None:
    """release.yml stages a draft for the version before it may go live; a stray tag must not reach a channel devices follow.

    A draft is not reachable by tag name, so this reads the release list.
    release.yml pushes the tag that triggers this run and only then stages the draft, so poll through that window.
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

    if channel not in CHANNEL_BRANCH:
        fail(f'Channel "{channel}" has no publishing rules for prod.')

    require_branch_contains(repo, channel, sha)
    if channel in DRAFT_CHANNELS:
        require_staged_draft(repo, version)

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
