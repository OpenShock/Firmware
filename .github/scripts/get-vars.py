#!/usr/bin/env python3
"""Derive the CI build variables: firmware version, release channel, build-variant matrix.

Python port of the former get-vars.js (no node/pnpm toolchain; semver is its one dependency).
Reads the git ref/sha from the GITHUB_* env, the pending version from release-tool (RELEASE_NEXT_VERSION / RELEASE_SKIP), the repo tags via git, and the buildable boards from boards/*.defaults, then writes GitHub Action outputs.
The boards are grouped into build variants (scripts/utils/boards.py): ci-build compiles once per variant and stamps each board into a copy.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

import semver

from gha import fail, notice, set_output
from repo_server_policy import CHANNELS

# The one parser for boards/<board>.defaults lives with the build helpers. ci-build's plan job checks out a
# sparse tree, so scripts/utils is listed there explicitly.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from utils.boards import all_boards, group_variants  # noqa: E402

def parse_version(tag: str) -> semver.Version | None:
    """Strict semver, except for the historical `v` prefix - the repo still carries a `v0.8.1` tag that the spec (and therefore the parser) rejects.
    Returns None rather than raising, since scanning the tag list is best-effort.
    """
    try:
        return semver.Version.parse(tag.strip().removeprefix('v'))
    except ValueError:
        return None


def prerelease_id(version: semver.Version) -> str:
    """First dot-separated prerelease identifier: `rc` out of `1.5.0-rc.2`, '' when the version is final.
    This is what names the channel.
    """
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

    # Version: release-tool's next version for branch/PR builds (labelled as a pre-release of the upcoming version), else the latest tag base.
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

    # A dispatch may name the channel; otherwise the ref decides.
    # Resolving it here rather than at publish time keeps one answer for the whole run, so what a build reports about itself is what it is published as.
    channel_input = os.environ.get('CHANNEL_INPUT', '').strip()
    if channel_input:
        if channel_input not in CHANNELS:
            fail(f'Channel "{channel_input}" is not one of: {", ".join(CHANNELS)}')
        if channel_input != channel:
            notice(f'Channel overridden by dispatch: {channel} -> {channel_input}')
        channel = channel_input
    elif channel not in CHANNELS:
        notice(f'Channel "{channel}" is not one the server knows; using develop.')
        channel = 'develop'

    # Boards: one per boards/<Board>.defaults fragment. A feature branch's own channel name is not one the
    # server knows, which is why it fell back to develop above.
    boards = all_boards()
    if not boards:
        fail('No boards/*.defaults files found in "boards"')

    # A caller may narrow the matrix (the pull request canary builds one board); empty means all.
    boards_input = [b.strip() for b in os.environ.get('BOARDS_INPUT', '').replace(',', '\n').split('\n') if b.strip()]
    if boards_input:
        unknown = sorted(set(boards_input) - set(boards))
        if unknown:
            fail(f'Unknown board(s) requested: {", ".join(unknown)}')
        boards = [b for b in boards if b in boards_input]

    # One matrix entry per build variant those boards need, so the variants follow from the boards asked for:
    # the pull request canary compiles its own variant only, and a full build every variant there is.
    # `include` alone is the whole matrix - there are no other axes to combine it with.
    # The board helpers report a bad fragment by raising SystemExit; route it through fail() for the annotation.
    try:
        variants = group_variants(boards)
    except SystemExit as err:
        fail(str(err.code))
    entries = [{'variant': v.id, 'chip': v.target, 'boards': ' '.join(v.boards)} for v in variants]

    print('Version:  ' + version)
    print('Channel:  ' + channel)
    print('Boards:   ' + ', '.join(boards))
    print('Tags:     ' + ', '.join(tags))
    print(f'Variants: {len(variants)} for {len(boards)} boards')
    for variant in variants:
        print(f'  {variant.id}  {", ".join(variant.boards)}')

    set_output('version', version)
    set_output('release-channel', channel)
    set_output('board-list', '\n'.join(boards))
    set_output('variant-matrix', json.dumps({'include': entries}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
