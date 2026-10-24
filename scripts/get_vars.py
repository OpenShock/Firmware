#!/usr/bin/env python3
"""Derive the CI build variables: firmware version, release channel, build-variant matrix.

Reads the git ref/sha from the GITHUB_* env, the pending version from release-tool (RELEASE_NEXT_VERSION / RELEASE_SKIP), the repo tags via git, and the buildable boards from boards/*.defaults, then writes GitHub Action outputs.
The boards are grouped into build variants (scripts/utils/boards.py): ci-build compiles once per variant and stamps each board into a copy.
"""

import json
import re
import subprocess
import sys

import semver
from repo_server_policy import CHANNELS

from utils.boards import all_boards, group_variants
from utils.gha import env, fail, notice, require_env, set_output, summary
from utils.github import parse_ref

CHANNEL_BY_PRERELEASE = {'': 'stable', 'stable': 'stable', 'rc': 'beta', 'beta': 'beta', 'dev': 'develop', 'develop': 'develop'}
CHANNEL_BY_BRANCH = {'master': 'stable', 'beta': 'beta', 'develop': 'develop'}


def parse_version(tag: str) -> semver.Version | None:
    """Strict semver, except for the historical `v` prefix (the repo still carries a `v0.8.1` tag).

    Returns None rather than raising, since scanning the tag list is best-effort.
    """
    try:
        return semver.Version.parse(tag.strip().removeprefix('v'))
    except ValueError:
        return None


def prerelease_id(version: semver.Version) -> str:
    """First dot-separated prerelease identifier (`rc` of `1.5.0-rc.2`, '' when final); it names the channel."""
    return (version.prerelease or '').split('.')[0]


def sanitize(name: str) -> str:
    return re.sub(r'[^a-zA-Z0-9]+', '-', name).strip('-')


def resolve_version(is_git_tag: bool, head_ref: str, short_sha: str, latest: semver.Version) -> str:
    next_version = env('RELEASE_NEXT_VERSION')

    # Branch and PR builds use release-tool's next version, labelled as a pre-release of it.
    if not is_git_tag and next_version and env('RELEASE_SKIP') != 'true':
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
    return version


def resolve_channel(is_git_tag: bool, head_ref: str, latest: semver.Version) -> str:
    if is_git_tag:
        channel = CHANNEL_BY_PRERELEASE.get(prerelease_id(latest))
        if channel is None:
            fail(f'Tag "{head_ref}" has an unrecognized prerelease channel')
    else:
        channel = CHANNEL_BY_BRANCH.get(head_ref) or sanitize(head_ref)

    # A manual dispatch may name the channel; derive keeps the one the ref maps to.
    channel_input = env('CHANNEL_INPUT')
    if channel_input not in ('', 'derive'):
        if channel_input not in CHANNELS:
            fail(f'Channel "{channel_input}" is not one of: {", ".join(CHANNELS)}')
        if channel_input != channel:
            notice(f'Channel overridden by dispatch: {channel} -> {channel_input}')
        return channel_input
    if channel not in CHANNELS:
        # A feature branch's own channel name is not one the server knows.
        notice(f'Channel "{channel}" is not one the server knows; using develop.')
        return 'develop'
    return channel


def select_boards() -> list[str]:
    """Every boards/<Board>.defaults fragment, narrowed to BOARDS_INPUT when set (the pull request canary builds one board)."""
    boards = all_boards()
    if not boards:
        fail('No boards/*.defaults files found in "boards"')

    boards_input = [b.strip() for b in env('BOARDS_INPUT').replace(',', '\n').split('\n') if b.strip()]
    if boards_input:
        unknown = sorted(set(boards_input) - set(boards))
        if unknown:
            fail(f'Unknown board(s) requested: {", ".join(unknown)}')
        boards = [b for b in boards if b in boards_input]
    return boards


def main() -> int:
    require_env('GITHUB_REF', 'GITHUB_SHA')
    git_ref = env('GITHUB_REF')

    kind, ref_name = parse_ref(git_ref)
    if not kind:
        fail(f'Git ref "{git_ref}" is not a valid branch, tag or pull request')
    is_git_tag = kind == 'tag'

    short_sha = env('GITHUB_SHA')[:8]

    head_ref = env('GITHUB_HEAD_REF') if kind == 'pull' else ref_name
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
        latest = parse_version(ref_name)
        if latest is None:
            fail(f'Git tag "{ref_name}" is not a valid semver version')
    else:
        latest = releases[0] if releases else semver.Version(0, 0, 0)

    version = resolve_version(is_git_tag, head_ref, short_sha, latest)
    channel = resolve_channel(is_git_tag, head_ref, latest)
    boards = select_boards()

    # `include` alone is the whole matrix: one entry per build variant the boards need.
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

    summary(
        f'### Plan\n\n- Version: `{version}`\n- Channel: `{channel}`\n- Boards: {", ".join(boards)}\n'
        f'- Variants: {len(variants)}'
    )
    set_output('version', version)
    set_output('release-channel', channel)
    set_output('board-list', '\n'.join(boards))
    set_output('variant-matrix', json.dumps({'include': entries}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
