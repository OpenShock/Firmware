#!/usr/bin/env python3
"""Derive the CI build variables: firmware version, release channel, board matrix.

Python port of the former get-vars.js (stdlib only, no node/pnpm toolchain).
Reads the git ref/sha from the GITHUB_* env, the pending version from
release-tool (RELEASE_NEXT_VERSION / RELEASE_SKIP), the repo tags via git, and
the buildable boards from boards/*.defaults, then writes GitHub Action outputs.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

import semver

from gha import fail, set_output

def parse_version(tag: str) -> semver.Version | None:
    """Strict semver, except for the historical `v` prefix - the repo still carries a
    `v0.8.1` tag that the spec (and therefore the parser) rejects. Returns None rather
    than raising, since scanning the tag list is best-effort."""
    try:
        return semver.Version.parse(tag.strip().removeprefix('v'))
    except ValueError:
        return None


def prerelease_id(version: semver.Version) -> str:
    """First dot-separated prerelease identifier: `rc` out of `1.5.0-rc.2`, '' when the
    version is final. This is what names the channel."""
    return (version.prerelease or '').split('.')[0]


def is_stable(version: semver.Version) -> bool:
    return prerelease_id(version) in ('', 'stable')


def is_beta(version: semver.Version) -> bool:
    return prerelease_id(version) in ('rc', 'beta')


def is_dev(version: semver.Version) -> bool:
    return prerelease_id(version) in ('dev', 'develop')


def sanitize(name: str) -> str:
    return re.sub(r'^-+|-+$', '', re.sub(r'-+', '-', re.sub(r'[^a-zA-Z0-9-]', '-', name)))


def main() -> int:
    git_ref = os.environ.get('GITHUB_REF')
    if git_ref is None:
        fail('Environment variable "GITHUB_REF" not found')

    is_git_tag = git_ref.startswith('refs/tags/')
    is_git_branch = git_ref.startswith('refs/heads/')
    is_git_pr = git_ref.startswith('refs/pull/') and git_ref.endswith('/merge')
    if not (is_git_tag or is_git_branch or is_git_pr):
        fail(f'Git ref "{git_ref}" is not a valid branch, tag or pull request')

    git_sha = os.environ.get('GITHUB_SHA')
    if git_sha is None:
        fail('Environment variable "GITHUB_SHA" not found')
    short_sha = git_sha[:8]

    head_ref = os.environ.get('GITHUB_HEAD_REF') if is_git_pr else git_ref.split('/', 2)[2]
    if not head_ref:
        fail('Failed to get git head ref name')

    try:
        tags_raw = subprocess.check_output(
            ['git', 'for-each-ref', '--sort=-creatordate',
             '--format=%(refname:short)', 'refs/tags'],
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        fail(f'Failed to read git tags: {e}')
    tags = [t.strip() for t in tags_raw.split('\n') if t.strip()]

    # Existing repo tags are best-effort: skip any that aren't strict semver.
    releases = [v for v in (parse_version(t) for t in tags) if v is not None]

    if is_git_tag:
        # A tag build's own tag is authoritative; an unparseable value is fatal.
        latest = parse_version(git_ref.split('/')[2])
        if latest is None:
            fail(f'Git tag "{git_ref.split("/")[2]}" is not a valid semver version')
    else:
        latest = releases[0] if releases else semver.Version(0, 0, 0)

    # Version: release-tool's next version for branch/PR builds (labelled as a
    # pre-release of the upcoming version), else the latest tag base.
    next_version = os.environ.get('RELEASE_NEXT_VERSION')
    release_skip = os.environ.get('RELEASE_SKIP') == 'true'

    if not is_git_tag and next_version and not release_skip:
        version = next_version
    else:
        version = str(latest.finalize_version())

    if not is_git_tag:
        suffix = sanitize(head_ref.split('/')[-1])
        if suffix:
            version += f'-{suffix}'
        version += f'+{short_sha}'
    else:
        if latest.prerelease:
            version += f'-{latest.prerelease}'
        if latest.build:
            version += f'+{latest.build}'

    # Release channel.
    if head_ref == 'master' or (is_git_tag and is_stable(latest)):
        channel = 'stable'
    elif head_ref == 'beta' or (is_git_tag and is_beta(latest)):
        channel = 'beta'
    elif head_ref == 'develop' or (is_git_tag and is_dev(latest)):
        channel = 'develop'
    elif is_git_tag:
        fail(f'Tag "{head_ref}" has an unrecognized prerelease channel')
    else:
        channel = sanitize(head_ref)

    # Board matrix: one entry per boards/<Board>.defaults fragment.
    boards_dir = Path('boards')
    if not boards_dir.is_dir():
        fail('Directory "boards" not found')
    boards = sorted(p.stem for p in boards_dir.glob('*.defaults'))
    if not boards:
        fail('No boards/*.defaults files found in "boards"')

    print('Version:  ' + version)
    print('Channel:  ' + channel)
    print('Boards:   ' + ', '.join(boards))
    print('Tags:     ' + ', '.join(tags))

    set_output('version', version)
    set_output('release-channel', channel)
    set_output('board-list', '\n'.join(boards))
    set_output('board-matrix', json.dumps({'board': boards}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
