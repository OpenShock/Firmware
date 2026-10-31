"""Tests for scripts/ci_build_gate.py: what each event builds and publishes, with the GitHub API faked."""

import json

import ci_build_gate
import pytest
from ci_build_gate import CANARY_BOARD, FULL_BUILD_LABEL, decide
from support import fails_with

SHA = 'c' * 40
BASE = 'b' * 40


class FakeApi:
    """Answers the gate's two API reads from canned data, and records what it was asked."""

    def __init__(self, runs=None, files=None, runs_error=None, compare_error=None):
        self.runs = runs or {}
        self.files = files or []
        self.runs_error = runs_error
        self.compare_error = compare_error
        self.run_queries = []
        self.compares = []

    def latest_successful_run(self, event, branch):
        self.run_queries.append((event, branch))
        if self.runs_error:
            raise self.runs_error
        return self.runs.get(event)

    def compare_files(self, base, head):
        self.compares.append((base, head))
        if self.compare_error:
            raise self.compare_error
        return [{'filename': f} if isinstance(f, str) else f for f in self.files]


def run(head_sha, created_at='2026-10-01T03:00:00Z'):
    return {'head_sha': head_sha, 'created_at': created_at}


def outcome(d):
    return (d.build, d.publish, d.server, d.boards)


def pr(action='synchronize', labels=(), label=None):
    payload = {'action': action, 'pull_request': {'labels': [{'name': n} for n in labels]}}
    if label is not None:
        payload['label'] = {'name': label}
    return payload


def nightly(branch, api):
    return decide('schedule', f'refs/heads/{branch}', SHA, {}, api)


# --- pull requests ---


@pytest.mark.parametrize('action', ['opened', 'reopened', 'synchronize'])
def test_pr_without_label_builds_the_canary(action):
    d = decide('pull_request', 'refs/pull/7/merge', SHA, pr(action), FakeApi())
    assert outcome(d) == (True, False, 'dev', CANARY_BOARD)


def test_pr_with_full_build_label_builds_every_board():
    d = decide('pull_request', 'refs/pull/7/merge', SHA, pr(labels=['bug', FULL_BUILD_LABEL]), FakeApi())
    assert outcome(d) == (True, False, 'dev', '')


def test_pr_labelled_full_build_builds_every_board():
    payload = pr('labeled', labels=[FULL_BUILD_LABEL], label=FULL_BUILD_LABEL)
    d = decide('pull_request', 'refs/pull/7/merge', SHA, payload, FakeApi())
    assert outcome(d) == (True, False, 'dev', '')


def test_pr_labelled_with_another_label_does_nothing(capsys):
    payload = pr('labeled', labels=['enhancement'], label='enhancement')
    d = decide('pull_request', 'refs/pull/7/merge', SHA, payload, FakeApi())
    assert outcome(d) == (False, False, 'dev', '')
    assert 'Label "enhancement" does not affect the build.' in capsys.readouterr().out


def test_pr_labelled_with_another_label_while_carrying_full_build_still_does_nothing():
    # The full build was already started by the labeled event for ci:full-build itself.
    payload = pr('labeled', labels=[FULL_BUILD_LABEL, 'bug'], label='bug')
    d = decide('pull_request', 'refs/pull/7/merge', SHA, payload, FakeApi())
    assert outcome(d) == (False, False, 'dev', '')


# --- release tags ---


@pytest.mark.parametrize('tag', ['1.5.0', '1.5.0-rc.1'])
@pytest.mark.parametrize('release_input', ['true', True])
def test_release_dispatch_builds_and_publishes_to_prod(tag, release_input):
    api = FakeApi()
    # release.yml leaves server at its dev default; a release goes to prod regardless.
    d = decide('workflow_dispatch', f'refs/tags/{tag}', SHA, {'inputs': {'release': release_input}}, api)
    assert outcome(d) == (True, True, 'prod', '')
    assert api.run_queries == [] and api.compares == []


def test_release_dispatch_on_a_branch_is_refused(capsys):
    with fails_with(capsys, 'A release was dispatched on refs/heads/master'):
        decide('workflow_dispatch', 'refs/heads/master', SHA, {'inputs': {'release': 'true'}}, FakeApi())


@pytest.mark.parametrize('event', ['push', 'release'])
def test_an_unsupported_event_is_refused(event, capsys):
    # Tag pushes no longer trigger ci-build: release.yml dispatches it once the draft is staged.
    with fails_with(capsys, f'Unsupported event {event!r}'):
        decide(event, 'refs/tags/1.5.0', SHA, {}, FakeApi())


# --- nightlies ---


def test_nightly_with_source_changes_builds_and_publishes():
    api = FakeApi(runs={'schedule': run(BASE)}, files=['README.md', 'components/rf/src/Foo.cpp'])
    d = nightly('develop', api)
    assert outcome(d) == (True, True, 'prod', '')
    assert api.run_queries == [('schedule', 'develop')]
    assert api.compares == [(BASE, SHA)]


def test_nightly_with_unchanged_sha_skips():
    api = FakeApi(runs={'schedule': run(SHA)})
    d = nightly('develop', api)
    assert outcome(d) == (False, False, 'prod', '')
    assert api.compares == []


NON_SOURCE_CHANGES = [
    'README.md',
    'components/rf/README.md',
    'docs/flashing.png',
    'LICENSE',
    'components/fs/data/.gitkeep',
    '.gitkeep',
    '.claude/settings.json',
    '.github/ISSUE_TEMPLATE/bug_report.yml',
    '.github/dependabot.yml',
    'schemas',
    'schemas/HubToGatewayMessage.fbs',
    'scripts/fetch_flatc.py',
    'scripts/generate_schemas.py',
    'scripts/flatc',
    'scripts/flatc.exe',
]


def test_nightly_with_only_non_source_changes_skips():
    api = FakeApi(runs={'schedule': run(BASE)}, files=NON_SOURCE_CHANGES)
    d = nightly('develop', api)
    assert outcome(d) == (False, False, 'prod', '')


@pytest.mark.parametrize('name', NON_SOURCE_CHANGES)
def test_non_source_paths(name):
    assert not ci_build_gate.is_source(name)


@pytest.mark.parametrize(
    'name',
    [
        '.changes/feat-rf.md',
        '.github/workflows/ci-build.yml',
        'scripts/get_vars.py',
        '.github/ISSUE_TEMPLATE',
        'components/rf/src/Foo.cpp',
        'docs',
        'LICENSE.txt',
        'mydocs/x.txt',
        'schemas.txt',
        'scripts/build.py',
        'scripts/flatc.sh',
        'sub/scripts/fetch_flatc.py',
        'CHANGELOG.mdx',
    ],
)
def test_source_paths(name):
    assert ci_build_gate.is_source(name)


@pytest.mark.parametrize('changed', [['.changes/feat-rf.md'], ['.github/workflows/ci-build.yml']])
def test_nightly_counts_changes_and_workflows_as_source(changed):
    api = FakeApi(runs={'schedule': run(BASE)}, files=changed)
    assert outcome(nightly('develop', api)) == (True, True, 'prod', '')


def test_nightly_counts_a_rename_from_source_as_source():
    api = FakeApi(
        runs={'schedule': run(BASE)},
        files=[{'filename': 'docs/old-notes.txt', 'previous_filename': 'components/rf/notes.txt'}],
    )
    assert nightly('develop', api).build


def test_first_ever_nightly_builds_without_comparing():
    api = FakeApi()
    d = nightly('develop', api)
    assert outcome(d) == (True, True, 'prod', '')
    assert api.compares == []


def test_nightly_run_listing_failure_builds(capsys):
    api = FakeApi(runs_error=RuntimeError('HTTP 502'), files=['README.md'])
    d = nightly('develop', api)
    assert outcome(d) == (True, True, 'prod', '')
    assert api.compares == []
    assert '::warning::Could not read the last develop build: HTTP 502' in capsys.readouterr().out


def test_nightly_compare_failure_builds(capsys):
    api = FakeApi(runs={'schedule': run(BASE)}, compare_error=RuntimeError('HTTP 404'))
    d = nightly('develop', api)
    assert outcome(d) == (True, True, 'prod', '')
    assert f'::warning::Could not compare {BASE}...{SHA}: HTTP 404' in capsys.readouterr().out


@pytest.mark.parametrize('count', [300, 301])
def test_nightly_with_a_truncated_comparison_builds(count):
    api = FakeApi(runs={'schedule': run(BASE)}, files=[f'docs/{i}.md' for i in range(count)])
    assert nightly('develop', api).build


def test_nightly_with_299_non_source_files_skips():
    api = FakeApi(runs={'schedule': run(BASE)}, files=[f'docs/{i}.md' for i in range(299)])
    assert not nightly('develop', api).build


@pytest.mark.parametrize('branch', ['beta', 'master', 'feat/arduino-3.0'])
def test_nightly_on_another_branch_is_refused(branch, capsys):
    api = FakeApi()
    d = nightly(branch, api)
    assert outcome(d) == (False, False, 'prod', '')
    assert api.run_queries == []
    assert f'Nightly requested on refs/heads/{branch}; only develop has a nightly.' in capsys.readouterr().out


def test_nightly_on_a_tag_is_refused():
    api = FakeApi()
    d = decide('schedule', 'refs/tags/1.5.0', SHA, {}, api)
    assert outcome(d) == (False, False, 'prod', '')
    assert api.run_queries == []


def test_nightly_ignores_a_manual_develop_build():
    api = FakeApi(runs={'workflow_dispatch': run(SHA), 'schedule': run(BASE)}, files=['main/main.cpp'])
    assert outcome(nightly('develop', api)) == (True, True, 'prod', '')


# --- manual dispatch ---


@pytest.mark.parametrize('nightly_input', ['false', False, None])
def test_dispatch_on_develop_publishes_to_dev(nightly_input):
    inputs = {'server': 'dev', 'channel': 'develop'}
    if nightly_input is not None:
        inputs['nightly'] = nightly_input
    api = FakeApi()
    d = decide('workflow_dispatch', 'refs/heads/develop', SHA, {'inputs': inputs}, api)
    assert outcome(d) == (True, True, 'dev', '')
    assert api.run_queries == []


def test_dispatch_on_master_with_prod_is_downgraded_to_dev(capsys):
    payload = {'inputs': {'server': 'prod', 'channel': 'stable', 'nightly': 'false'}}
    d = decide('workflow_dispatch', 'refs/heads/master', SHA, payload, FakeApi())
    assert outcome(d) == (True, True, 'dev', '')
    assert 'prod was requested on refs/heads/master' in capsys.readouterr().out


def test_dispatch_on_develop_with_prod_is_downgraded_to_dev():
    payload = {'inputs': {'server': 'prod', 'nightly': 'false'}}
    d = decide('workflow_dispatch', 'refs/heads/develop', SHA, payload, FakeApi())
    assert outcome(d) == (True, True, 'dev', '')


def test_dispatch_on_a_tag_may_publish_to_prod():
    payload = {'inputs': {'server': 'prod', 'channel': 'beta', 'nightly': 'false'}}
    d = decide('workflow_dispatch', 'refs/tags/1.5.0-rc.1', SHA, payload, FakeApi())
    assert outcome(d) == (True, True, 'prod', '')


def test_dispatch_without_inputs_publishes_to_dev():
    d = decide('workflow_dispatch', 'refs/heads/feat/x', SHA, {}, FakeApi())
    assert outcome(d) == (True, True, 'dev', '')


# --- the real API wrapper, over a faked github_get ---


@pytest.fixture
def github_get(monkeypatch):
    """Answers ci_build_gate.github_get with a canned document; records (path, token, params)."""
    calls = []
    answer = {}

    def fake(path, token, params=None):
        calls.append((path, token, params))
        return answer

    monkeypatch.setattr(ci_build_gate, 'github_get', fake)
    return answer, calls


def test_latest_successful_run_asks_for_one_successful_run(github_get):
    answer, calls = github_get
    api = ci_build_gate.GitHubApi('OpenShock/Firmware', 'tok')
    assert api.latest_successful_run('schedule', 'develop') is None
    answer['workflow_runs'] = [run(BASE)]
    assert api.latest_successful_run('schedule', 'develop') == run(BASE)
    path, token, params = calls[0]
    assert path == '/repos/OpenShock/Firmware/actions/workflows/ci-build.yml/runs'
    assert token == 'tok'
    assert params == {'event': 'schedule', 'branch': 'develop', 'status': 'success', 'per_page': 1}


def test_compare_files_tolerates_a_comparison_without_files(github_get):
    answer, calls = github_get
    api = ci_build_gate.GitHubApi('OpenShock/Firmware', 'tok')
    assert api.compare_files(BASE, SHA) == []
    answer['files'] = [{'filename': 'a.cpp'}]
    assert api.compare_files(BASE, SHA) == [{'filename': 'a.cpp'}]
    assert calls[0][0] == f'/repos/OpenShock/Firmware/compare/{BASE}...{SHA}'
    assert calls[0][2] == {'per_page': ci_build_gate.COMPARE_LIMIT}


# --- main ---


def test_main_writes_every_output(tmp_path, monkeypatch):
    event = tmp_path / 'event.json'
    event.write_text(json.dumps(pr()), encoding='utf-8')
    out = tmp_path / 'output'
    monkeypatch.setenv('GITHUB_EVENT_NAME', 'pull_request')
    monkeypatch.setenv('GITHUB_REF', 'refs/pull/7/merge')
    monkeypatch.setenv('GITHUB_SHA', SHA)
    monkeypatch.setenv('GITHUB_REPOSITORY', 'OpenShock/Firmware')
    monkeypatch.setenv('GITHUB_EVENT_PATH', str(event))
    monkeypatch.setenv('GITHUB_OUTPUT', str(out))
    monkeypatch.setenv('GITHUB_STEP_SUMMARY', str(tmp_path / 'summary'))
    assert ci_build_gate.main() == 0
    assert f'- Boards: `{CANARY_BOARD}`' in (tmp_path / 'summary').read_text(encoding='utf-8')
    assert out.read_text(encoding='utf-8').splitlines() == [
        'build=true',
        'publish=false',
        'server=dev',
        f'boards={CANARY_BOARD}',
    ]
