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
from typing import NoReturn

MIN_PYTHON = (3, 12, 3)
if sys.version_info < MIN_PYTHON:
    raise SystemExit(
        'get-vars requires Python >= %s, got %s'
        % ('.'.join(map(str, MIN_PYTHON)), sys.version.split()[0])
    )

SEMVER_RE = re.compile(
    r'^v?(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)'
    r'(?:-(?P<prerelease>[0-9A-Za-z.-]+))?(?:\+(?P<build>[0-9A-Za-z.-]+))?$'
)


def set_failed(msg: str) -> NoReturn:
    print(f'::error::{msg}')
    sys.exit(1)


def set_output(name: str, value: str):
    gh_output = os.environ.get('GITHUB_OUTPUT')
    if not gh_output:
        print(f'[output] {name}={value}')
        return
    with open(gh_output, 'a', encoding='utf-8') as f:
        if '\n' in value:
            delim = f'__EOF_{name}__'
            f.write(f'{name}<<{delim}\n{value}\n{delim}\n')
        else:
            f.write(f'{name}={value}\n')


class SemVer:
    def __init__(self, major, minor, patch, prerelease, build):
        self.major, self.minor, self.patch = major, minor, patch
        # prerelease/build as dot-separated identifier lists (like node-semver).
        self.prerelease = prerelease.split('.') if prerelease else []
        self.build = build.split('.') if build else []

    @classmethod
    def parse(cls, tag: str):
        m = SEMVER_RE.match(tag.strip())
        if not m:
            return None
        return cls(int(m['major']), int(m['minor']), int(m['patch']),
                   m['prerelease'], m['build'])

    @property
    def base(self) -> str:
        return f'{self.major}.{self.minor}.{self.patch}'

    def is_stable(self) -> bool:
        return len(self.prerelease) == 0 or self.prerelease[0] == 'stable'

    def is_beta(self) -> bool:
        return len(self.prerelease) > 0 and self.prerelease[0] in ('rc', 'beta')

    def is_dev(self) -> bool:
        return len(self.prerelease) > 0 and self.prerelease[0] in ('dev', 'develop')


def sanitize(name: str) -> str:
    return re.sub(r'^-+|-+$', '', re.sub(r'-+', '-', re.sub(r'[^a-zA-Z0-9-]', '-', name)))


def main() -> int:
    git_ref = os.environ.get('GITHUB_REF')
    if git_ref is None:
        set_failed('Environment variable "GITHUB_REF" not found')

    is_git_tag = git_ref.startswith('refs/tags/')
    is_git_branch = git_ref.startswith('refs/heads/')
    is_git_pr = git_ref.startswith('refs/pull/') and git_ref.endswith('/merge')
    if not (is_git_tag or is_git_branch or is_git_pr):
        set_failed(f'Git ref "{git_ref}" is not a valid branch, tag or pull request')

    git_sha = os.environ.get('GITHUB_SHA')
    if git_sha is None:
        set_failed('Environment variable "GITHUB_SHA" not found')
    short_sha = git_sha[:8]

    head_ref = os.environ.get('GITHUB_HEAD_REF') if is_git_pr else git_ref.split('/', 2)[2]
    if not head_ref:
        set_failed('Failed to get git head ref name')

    try:
        tags_raw = subprocess.check_output(
            ['git', 'for-each-ref', '--sort=-creatordate',
             '--format=%(refname:short)', 'refs/tags'],
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        set_failed(f'Failed to read git tags: {e}')
    tags = [t.strip() for t in tags_raw.split('\n') if t.strip()]

    # Existing repo tags are best-effort: skip any that aren't strict semver.
    releases = [s for s in (SemVer.parse(t) for t in tags) if s is not None]

    if is_git_tag:
        # A tag build's own tag is authoritative; an unparseable value is fatal.
        latest = SemVer.parse(git_ref.split('/')[2])
        if latest is None:
            set_failed(f'Git tag "{git_ref.split("/")[2]}" is not a valid semver version')
    else:
        latest = releases[0] if releases else SemVer.parse('0.0.0')

    # Version: release-tool's next version for branch/PR builds (labelled as a
    # pre-release of the upcoming version), else the latest tag base.
    next_version = os.environ.get('RELEASE_NEXT_VERSION')
    release_skip = os.environ.get('RELEASE_SKIP') == 'true'

    if not is_git_tag and next_version and not release_skip:
        version = next_version
    else:
        version = latest.base

    if not is_git_tag:
        suffix = sanitize(head_ref.split('/')[-1])
        if suffix:
            version += f'-{suffix}'
        version += f'+{short_sha}'
    else:
        if latest.prerelease:
            version += '-' + '.'.join(latest.prerelease)
        if latest.build:
            version += '+' + '.'.join(latest.build)

    # Release channel.
    if head_ref == 'master' or (is_git_tag and latest.is_stable()):
        channel = 'stable'
    elif head_ref == 'beta' or (is_git_tag and latest.is_beta()):
        channel = 'beta'
    elif head_ref == 'develop' or (is_git_tag and latest.is_dev()):
        channel = 'develop'
    elif is_git_tag:
        set_failed(f'Tag "{head_ref}" has an unrecognized prerelease channel')
    else:
        channel = sanitize(head_ref)

    # Board matrix: one entry per boards/<Board>.defaults fragment.
    boards_dir = Path('boards')
    if not boards_dir.is_dir():
        set_failed('Directory "boards" not found')
    boards = sorted(p.stem for p in boards_dir.glob('*.defaults'))
    if not boards:
        set_failed('No boards/*.defaults files found in "boards"')

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
