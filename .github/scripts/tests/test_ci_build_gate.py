import json

import pytest

import ci_build_gate
from ci_build_gate import CANARY_BOARD, FULL_BUILD_LABEL, decide

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
    return (d.build, d.deploy, d.publish, d.server, d.boards)


def pr(action='synchronize', labels=(), label=None):
    payload = {'action': action, 'pull_request': {'labels': [{'name': n} for n in labels]}}
    if label is not None:
        payload['label'] = {'name': label}
    return payload


def nightly(branch, api, inputs=None):
    payload = {'inputs': {'nightly': 'true', **(inputs or {})}}
    return decide('workflow_dispatch', f'refs/heads/{branch}', SHA, payload, api)


# --- pull requests ---


@pytest.mark.parametrize('action', ['opened', 'reopened', 'synchronize'])
def test_pr_without_label_builds_the_canary(action):
    d = decide('pull_request', 'refs/pull/7/merge', SHA, pr(action), FakeApi())
    assert outcome(d) == (True, False, False, 'dev', CANARY_BOARD)
    assert not d.warm_idf


def test_pr_with_full_build_label_builds_every_board():
    d = decide('pull_request', 'refs/pull/7/merge', SHA, pr(labels=['bug', FULL_BUILD_LABEL]), FakeApi())
    assert outcome(d) == (True, False, False, 'dev', '')
    assert d.warm_idf


def test_pr_labelled_full_build_builds_every_board():
    payload = pr('labeled', labels=[FULL_BUILD_LABEL], label=FULL_BUILD_LABEL)
    d = decide('pull_request', 'refs/pull/7/merge', SHA, payload, FakeApi())
    assert outcome(d) == (True, False, False, 'dev', '')


def test_pr_labelled_with_another_label_does_nothing(capsys):
    payload = pr('labeled', labels=['enhancement'], label='enhancement')
    d = decide('pull_request', 'refs/pull/7/merge', SHA, payload, FakeApi())
    assert outcome(d) == (False, False, False, 'dev', '')
    assert not d.warm_idf
    assert 'Label "enhancement" does not affect the build.' in capsys.readouterr().out


def test_pr_labelled_with_another_label_while_carrying_full_build_still_does_nothing():
    # The full build was already started by the labeled event for ci:full-build itself.
    payload = pr('labeled', labels=[FULL_BUILD_LABEL, 'bug'], label='bug')
    d = decide('pull_request', 'refs/pull/7/merge', SHA, payload, FakeApi())
    assert outcome(d) == (False, False, False, 'dev', '')


# --- release tags ---


@pytest.mark.parametrize('tag', ['1.5.0', '1.5.0-rc.1', '1.5.0-develop.3'])
def test_tag_push_builds_deploys_and_publishes_to_prod(tag):
    api = FakeApi()
    d = decide('push', f'refs/tags/{tag}', SHA, {}, api)
    assert outcome(d) == (True, True, True, 'prod', '')
    assert d.warm_idf
    assert api.run_queries == [] and api.compares == []


# --- nightlies ---


@pytest.mark.parametrize(
    'branch, deploy, publish',
    [('develop', True, True), ('beta', False, False), ('master', False, False)],
)
def test_nightly_with_source_changes_builds(branch, deploy, publish):
    api = FakeApi(runs={'workflow_dispatch': run(BASE)}, files=['README.md', 'components/rf/src/Foo.cpp'])
    d = nightly(branch, api)
    assert outcome(d) == (True, deploy, publish, 'prod', '')
    assert d.warm_idf
    assert api.run_queries == [('schedule', branch), ('workflow_dispatch', branch)]
    assert api.compares == [(BASE, SHA)]


@pytest.mark.parametrize('branch', ['develop', 'beta', 'master'])
def test_nightly_with_unchanged_sha_skips(branch):
    api = FakeApi(runs={'workflow_dispatch': run(SHA)})
    d = nightly(branch, api)
    assert outcome(d) == (False, False, False, 'prod', '')
    assert not d.warm_idf
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


@pytest.mark.parametrize('branch', ['develop', 'beta', 'master'])
def test_nightly_with_only_non_source_changes_skips(branch):
    api = FakeApi(runs={'schedule': run(BASE)}, files=NON_SOURCE_CHANGES)
    d = nightly(branch, api)
    assert outcome(d) == (False, False, False, 'prod', '')


@pytest.mark.parametrize('name', NON_SOURCE_CHANGES)
def test_non_source_paths(name):
    assert not ci_build_gate.is_source(name)


@pytest.mark.parametrize(
    'name',
    [
        '.changes/feat-rf.md',
        '.github/workflows/ci-build.yml',
        '.github/scripts/get-vars.py',
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
    api = FakeApi(runs={'workflow_dispatch': run(BASE)}, files=changed)
    assert outcome(nightly('develop', api)) == (True, True, True, 'prod', '')


def test_nightly_counts_a_rename_from_source_as_source():
    api = FakeApi(
        runs={'workflow_dispatch': run(BASE)},
        files=[{'filename': 'docs/old-notes.txt', 'previous_filename': 'components/rf/notes.txt'}],
    )
    assert nightly('develop', api).build


def test_first_ever_nightly_builds_without_comparing():
    api = FakeApi()
    d = nightly('develop', api)
    assert outcome(d) == (True, True, True, 'prod', '')
    assert api.compares == []


def test_nightly_compares_against_the_newer_of_schedule_and_dispatch():
    older, newer = 'a' * 40, 'd' * 40
    api = FakeApi(
        runs={
            'schedule': run(older, '2026-09-01T03:00:00Z'),
            'workflow_dispatch': run(newer, '2026-10-01T03:00:00Z'),
        },
        files=['README.md'],
    )
    nightly('develop', api)
    assert api.compares == [(newer, SHA)]

    api.runs = {'schedule': run(newer, '2026-10-02T03:00:00Z'), 'workflow_dispatch': run(older, '2026-10-01T03:00:00Z')}
    api.compares = []
    nightly('develop', api)
    assert api.compares == [(newer, SHA)]


def test_nightly_run_listing_failure_builds(capsys):
    api = FakeApi(runs_error=RuntimeError('HTTP 502'), files=['README.md'])
    d = nightly('develop', api)
    assert outcome(d) == (True, True, True, 'prod', '')
    assert api.compares == []
    assert '::warning::Could not read the last develop build: HTTP 502' in capsys.readouterr().out


def test_nightly_compare_failure_builds(capsys):
    api = FakeApi(runs={'workflow_dispatch': run(BASE)}, compare_error=RuntimeError('HTTP 404'))
    d = nightly('beta', api)
    assert outcome(d) == (True, False, False, 'prod', '')
    assert f'::warning::Could not compare {BASE}...{SHA}: HTTP 404' in capsys.readouterr().out


@pytest.mark.parametrize('count', [300, 301])
def test_nightly_with_a_truncated_comparison_builds(count):
    api = FakeApi(runs={'workflow_dispatch': run(BASE)}, files=[f'docs/{i}.md' for i in range(count)])
    assert nightly('master', api).build


def test_nightly_with_299_non_source_files_skips():
    api = FakeApi(runs={'workflow_dispatch': run(BASE)}, files=[f'docs/{i}.md' for i in range(299)])
    assert not nightly('master', api).build


def test_nightly_on_a_feature_branch_is_refused(capsys):
    api = FakeApi()
    d = nightly('feat/arduino-3.0', api)
    assert outcome(d) == (False, False, False, 'prod', '')
    assert api.run_queries == []
    assert 'Nightly requested on refs/heads/feat/arduino-3.0' in capsys.readouterr().out


def test_nightly_on_a_tag_is_refused():
    api = FakeApi()
    d = decide('workflow_dispatch', 'refs/tags/1.5.0', SHA, {'inputs': {'nightly': 'true'}}, api)
    assert outcome(d) == (False, False, False, 'prod', '')
    assert api.run_queries == []


def test_nightly_ignores_server_and_channel():
    api = FakeApi(runs={'workflow_dispatch': run(BASE)}, files=['main/main.cpp'])
    d = nightly('beta', api, {'server': 'dev', 'channel': 'stable'})
    assert outcome(d) == (True, False, False, 'prod', '')


def test_boolean_nightly_input_counts():
    api = FakeApi()
    d = decide('workflow_dispatch', 'refs/heads/develop', SHA, {'inputs': {'nightly': True}}, api)
    assert d.server == 'prod' and api.run_queries


# --- manual dispatch ---


@pytest.mark.parametrize('nightly_input', ['false', False, None])
def test_dispatch_on_develop_publishes_to_dev(nightly_input):
    inputs = {'server': 'dev', 'channel': 'develop'}
    if nightly_input is not None:
        inputs['nightly'] = nightly_input
    api = FakeApi()
    d = decide('workflow_dispatch', 'refs/heads/develop', SHA, {'inputs': inputs}, api)
    assert outcome(d) == (True, True, True, 'dev', '')
    assert api.run_queries == []


def test_dispatch_on_master_with_prod_is_downgraded_to_dev(capsys):
    payload = {'inputs': {'server': 'prod', 'channel': 'stable', 'nightly': 'false'}}
    d = decide('workflow_dispatch', 'refs/heads/master', SHA, payload, FakeApi())
    assert outcome(d) == (True, False, True, 'dev', '')
    assert 'prod was requested on refs/heads/master' in capsys.readouterr().out


def test_dispatch_on_develop_with_prod_is_downgraded_to_dev():
    payload = {'inputs': {'server': 'prod', 'nightly': 'false'}}
    d = decide('workflow_dispatch', 'refs/heads/develop', SHA, payload, FakeApi())
    assert outcome(d) == (True, True, True, 'dev', '')


def test_dispatch_on_a_tag_may_publish_to_prod():
    payload = {'inputs': {'server': 'prod', 'channel': 'beta', 'nightly': 'false'}}
    d = decide('workflow_dispatch', 'refs/tags/1.5.0-rc.1', SHA, payload, FakeApi())
    assert outcome(d) == (True, False, True, 'prod', '')


def test_dispatch_without_inputs_publishes_to_dev():
    d = decide('workflow_dispatch', 'refs/heads/feat/x', SHA, {}, FakeApi())
    assert outcome(d) == (True, False, True, 'dev', '')


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
    assert ci_build_gate.main() == 0
    assert out.read_text(encoding='utf-8').splitlines() == [
        'build=true',
        'deploy=false',
        'publish=false',
        'server=dev',
        f'boards={CANARY_BOARD}',
        'warm-idf=false',
    ]
