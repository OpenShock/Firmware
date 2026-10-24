#!/usr/bin/env python3
"""Decide what a ci-build run does: build, publish, which server, which boards.

Building is not publishing: a pull request compiles and goes no further.
The GitHub API is queried only for the nightly (when develop last built, and what changed since); those calls sit behind the Api protocol so tests need no network.
"""

import json
import re
from dataclasses import dataclass
from typing import Protocol

from utils.gha import env, notice, require_env, set_output, summary, warn
from utils.github import parse_ref
from utils.github_api import github_get

FULL_BUILD_LABEL = 'ci:full-build'
CANARY_BOARD = 'OpenShock-Core-V2'

# A comparison reports at most this many files; a full page may be truncated, so it counts as a change.
COMPARE_LIMIT = 300

# Paths that cannot change a firmware image or its build. The nightly skips when nothing else changed.
# .changes/ and .github/ count as source: change files set the version, and a CI change should
# meet a nightly before it meets a release tag.
NON_SOURCE = [
    re.compile(r'^(?!\.changes/).*\.md$'),
    re.compile(r'^docs/'),
    re.compile(r'^LICENSE$'),
    re.compile(r'(^|/)\.gitkeep$'),
    re.compile(r'^\.claude/'),
    re.compile(r'^\.github/ISSUE_TEMPLATE/'),
    re.compile(r'^\.github/dependabot\.yml$'),
    # Dev-only schema submodule and its codegen helpers; builds use the generated sources.
    re.compile(r'^schemas(/|$)'),
    re.compile(r'^scripts/(fetch_flatc\.py|generate_schemas\.py|flatc|flatc\.exe)$'),
]


class Api(Protocol):
    """The two GitHub API reads the nightly needs. Either may raise; the gate decides what a failure means."""

    def latest_successful_run(self, event: str, branch: str) -> dict | None:
        """The newest successful ci-build run of this event on this branch, or None."""
        ...

    def compare_files(self, base: str, head: str) -> list[dict]:
        """The files changed between base and head, as the compare API reports them."""
        ...


class GitHubApi:
    def __init__(self, repo: str, token: str) -> None:
        self.repo = repo
        self.token = token

    def _get(self, path: str, params: dict) -> dict:
        return github_get(f'/repos/{self.repo}{path}', self.token, params)

    def latest_successful_run(self, event: str, branch: str) -> dict | None:
        data = self._get(
            '/actions/workflows/ci-build.yml/runs',
            {'event': event, 'branch': branch, 'status': 'success', 'per_page': 1},
        )
        runs = data.get('workflow_runs') or []
        return runs[0] if runs else None

    def compare_files(self, base: str, head: str) -> list[dict]:
        return self._get(f'/compare/{base}...{head}', {'per_page': COMPARE_LIMIT}).get('files') or []


@dataclass
class Decision:
    build: bool = False
    publish: bool = False
    server: str = 'dev'
    boards: str = ''


def last_build_sha(api: Api, branch: str) -> str:
    """HEAD of this branch's last successful nightly.

    Returns '' on error so a hiccup rebuilds rather than wrongly skipping.
    """
    try:
        run = api.latest_successful_run('schedule', branch)
        return (run or {}).get('head_sha') or ''
    except Exception as err:
        warn(f'Could not read the last {branch} build: {err}')
        return ''


def is_source(name: str) -> bool:
    return not any(p.search(name) for p in NON_SOURCE)


def source_changed_since(api: Api, base: str, sha: str) -> bool:
    """Whether anything between base and sha can change the firmware.

    Errs towards true: an unreadable or truncated comparison builds rather than skips.
    """
    try:
        files = api.compare_files(base, sha)
        if len(files) >= COMPARE_LIMIT:
            return True
        names = [n for f in files for n in (f.get('filename'), f.get('previous_filename')) if n]
        source = [n for n in names if is_source(n)]
        if source:
            print(f'Firmware sources changed: {", ".join(source[:20])}')
        return bool(source)
    except Exception as err:
        warn(f'Could not compare {base}...{sha}: {err}')
        return True


def _decide_pull_request(payload: dict) -> Decision:
    # A pull request compiles and stops: merging changes nothing on any server.
    label = (payload.get('label') or {}).get('name')
    if payload.get('action') == 'labeled' and label != FULL_BUILD_LABEL:
        notice(f'Label "{label}" does not affect the build.')
        return Decision()
    d = Decision(build=True)
    labels = (payload.get('pull_request') or {}).get('labels') or []
    if not any(lbl.get('name') == FULL_BUILD_LABEL for lbl in labels):
        d.boards = CANARY_BOARD
        notice(f'Canary build of {CANARY_BOARD} only; add the {FULL_BUILD_LABEL} label to build every board.')
    return d


def _decide_nightly(ref: str, sha: str, api: Api) -> Decision:
    # Only develop has a nightly; beta and master are built and published from tags.
    kind, branch = parse_ref(ref)
    d = Decision(server='prod')
    if kind != 'branch' or branch != 'develop':
        warn(f'Nightly requested on {ref}; only develop has a nightly.')
        return d
    base = last_build_sha(api, branch)
    if base == sha:
        notice(f'{branch} unchanged since its last build ({sha}); nothing to build.')
    elif base != '' and not source_changed_since(api, base, sha):
        notice(f'No firmware sources changed on {branch} since its last build ({base}); nothing to build.')
    else:
        d.build = True
    d.publish = d.build
    return d


def _decide_dispatch(ref: str, inputs: dict) -> Decision:
    # A manual dispatch always publishes; the channel and the ref are chosen by hand.
    d = Decision(build=True, publish=True, server='prod' if inputs.get('server') == 'prod' else 'dev')
    # The ref picker offers tags as well as branches, so prod still means running against the tag being released.
    if d.server == 'prod' and parse_ref(ref)[0] != 'tag':
        warn(f'prod was requested on {ref}; only a tag may dispatch to prod, publishing to dev instead.')
        d.server = 'dev'
    return d


def _decide_push(ref: str) -> Decision:
    # The workflow only triggers on release tags, which publish to prod.
    is_tag = parse_ref(ref)[0] == 'tag'
    return Decision(build=True, publish=is_tag, server='prod' if is_tag else 'dev')


def decide(event_name: str, ref: str, sha: str, payload: dict, api: Api) -> Decision:
    if event_name == 'pull_request':
        return _decide_pull_request(payload)
    if event_name == 'schedule':
        return _decide_nightly(ref, sha, api)
    if event_name == 'workflow_dispatch':
        return _decide_dispatch(ref, payload.get('inputs') or {})
    if event_name == 'push':
        return _decide_push(ref)
    raise SystemExit(f'error: unsupported event {event_name!r}')


def main() -> int:
    require_env('GITHUB_EVENT_NAME', 'GITHUB_REF', 'GITHUB_SHA', 'GITHUB_REPOSITORY', 'GITHUB_EVENT_PATH')

    with open(env('GITHUB_EVENT_PATH'), encoding='utf-8') as f:
        payload = json.load(f)

    api = GitHubApi(env('GITHUB_REPOSITORY'), env('GITHUB_TOKEN'))
    d = decide(env('GITHUB_EVENT_NAME'), env('GITHUB_REF'), env('GITHUB_SHA'), payload, api)

    set_output('build', str(d.build).lower())
    set_output('publish', str(d.publish).lower())
    set_output('server', d.server)
    set_output('boards', d.boards)
    decision = (
        f'build={str(d.build).lower()} publish={str(d.publish).lower()} server={d.server} boards={d.boards or "all"}'
    )
    print(f'decision: {decision}')
    summary(f'### Gate\n\n`{decision}`')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
