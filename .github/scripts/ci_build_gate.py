#!/usr/bin/env python3
"""Decide what a ci-build run does: build, deploy, publish, which server, which boards.

build governs the build matrix.
deploy additionally cuts a GitHub release, and implies build.
publish governs whether the build reaches a repository server at all, and server names which one.
Building is not publishing: a pull request compiles and goes no further.

Reads the event from the GITHUB_* environment and the event payload, and asks the GitHub API only for a nightly: when this branch last built, and whether anything since then can change the firmware.
Those two calls sit behind the Api protocol so the tests can answer them without a network.
"""

import json
import re
from dataclasses import dataclass
from datetime import datetime
from typing import Protocol

import requests

from gha import env, notice, require_env, set_output, warn
from repo_server_policy import API, API_VERSION

FULL_BUILD_LABEL = 'ci:full-build'
CANARY_BOARD = 'OpenShock-Core-V2'
NIGHTLY_BRANCHES = ('develop', 'beta', 'master')

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
        self.session = requests.Session()
        self.session.headers.update(
            {
                'Authorization': f'Bearer {token}',
                'Accept': 'application/vnd.github+json',
                'X-GitHub-Api-Version': API_VERSION,
            }
        )

    def _get(self, path: str, params: dict) -> dict:
        resp = self.session.get(f'{API}/repos/{self.repo}{path}', params=params, timeout=(10, 30))
        if resp.status_code != 200:
            raise RuntimeError(f'HTTP {resp.status_code}: {(resp.text or "").strip()[:300]}')
        return resp.json()

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
    deploy: bool = False
    publish: bool = False
    server: str = 'dev'
    # Empty builds every board.
    boards: str = ''

    @property
    def warm_idf(self) -> bool:
        """Whether to warm the ESP-IDF cache before the matrix.

        Only worth it when several boards would otherwise miss the cache at once; a single board's job writes the cache itself as the nominated writer.
        """
        return self.build and len(self.boards.split()) != 1


def _created_at(run: dict) -> datetime:
    return datetime.fromisoformat(run['created_at'])


def last_build_sha(api: Api, branch: str) -> str:
    """HEAD of this branch's last successful nightly or manual run.

    'schedule' covers the develop nightlies from before nightly.yml dispatched them.
    Returns '' on error so a hiccup rebuilds rather than wrongly skipping.
    """
    try:
        runs = [r for r in (api.latest_successful_run(e, branch) for e in ('schedule', 'workflow_dispatch')) if r]
        if not runs:
            return ''
        runs.sort(key=_created_at, reverse=True)
        return runs[0].get('head_sha') or ''
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
            print(f"Firmware sources changed: {', '.join(source[:20])}")
        return bool(source)
    except Exception as err:
        warn(f'Could not compare {base}...{sha}: {err}')
        return True


def decide(event_name: str, ref: str, sha: str, payload: dict, api: Api) -> Decision:
    d = Decision()
    branch = ref.removeprefix('refs/heads/') if ref.startswith('refs/heads/') else ''
    inputs = payload.get('inputs') or {}
    # The payload carries a boolean input as either true or 'true', depending on who dispatched it.
    nightly = event_name == 'workflow_dispatch' and inputs.get('nightly') in (True, 'true')

    if event_name == 'pull_request':
        label = (payload.get('label') or {}).get('name')
        if payload.get('action') == 'labeled' and label != FULL_BUILD_LABEL:
            notice(f'Label "{label}" does not affect the build.')
        else:
            d.build = True
            labels = (payload.get('pull_request') or {}).get('labels') or []
            if not any(lbl.get('name') == FULL_BUILD_LABEL for lbl in labels):
                d.boards = CANARY_BOARD
                notice(f'Canary build of {CANARY_BOARD} only; add the {FULL_BUILD_LABEL} label to build every board.')
    elif event_name == 'push':
        # Only release tags trigger a push run.
        d.build = True
        d.deploy = True
    elif event_name == 'workflow_dispatch':
        if nightly:
            if branch not in NIGHTLY_BRANCHES:
                warn(f'Nightly requested on {ref}; only develop, beta and master have nightlies.')
            else:
                base = last_build_sha(api, branch)
                if base == sha:
                    notice(f'{branch} unchanged since its last build ({sha}); nothing to build.')
                elif base != '' and not source_changed_since(api, base, sha):
                    notice(f'No firmware sources changed on {branch} since its last build ({base}); nothing to build.')
                else:
                    d.build = True
                    # Only develop's nightly is a release; beta and master publish from tags.
                    d.deploy = branch == 'develop'
        else:
            d.build = True
            d.deploy = ref == 'refs/heads/develop'
    else:
        d.build = True

    # Publishing is never a side effect of a build.
    # A pull request compiles and stops, so merging changes nothing on any server until someone says to.
    # The develop nightly and a release tag publish to prod, because both are already the deliberate act.
    # The beta and master nightlies only build: those channels are published from tags.
    # Everything else reaches a server only by being dispatched, which is where the channel and the ref are chosen by hand.
    is_tag = ref.startswith('refs/tags/')

    if nightly:
        d.publish = d.build and branch == 'develop'
        d.server = 'prod'
    elif event_name == 'workflow_dispatch':
        d.publish = True
        d.server = 'prod' if inputs.get('server') == 'prod' else 'dev'
        # The ref picker offers tags as well as branches, so reaching prod by hand still means running against the tag being released.
        if d.server == 'prod' and not is_tag:
            warn(f'prod was requested on {ref}; only a tag may dispatch to prod, publishing to dev instead.')
            d.server = 'dev'
    elif event_name == 'push' and is_tag:
        d.publish = True
        d.server = 'prod'

    return d


def main() -> int:
    require_env('GITHUB_EVENT_NAME', 'GITHUB_REF', 'GITHUB_SHA', 'GITHUB_REPOSITORY', 'GITHUB_EVENT_PATH')

    with open(env('GITHUB_EVENT_PATH'), encoding='utf-8') as f:
        payload = json.load(f)

    api = GitHubApi(env('GITHUB_REPOSITORY'), env('GITHUB_TOKEN'))
    d = decide(env('GITHUB_EVENT_NAME'), env('GITHUB_REF'), env('GITHUB_SHA'), payload, api)

    def flag(value: bool) -> str:
        return 'true' if value else 'false'

    set_output('build', flag(d.build))
    set_output('deploy', flag(d.deploy))
    set_output('publish', flag(d.publish))
    set_output('server', d.server)
    set_output('boards', d.boards)
    set_output('warm-idf', flag(d.warm_idf))
    print(
        f'decision: build={flag(d.build)} deploy={flag(d.deploy)} publish={flag(d.publish)} '
        f'server={d.server} boards={d.boards or "all"} warm-idf={flag(d.warm_idf)}'
    )
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
