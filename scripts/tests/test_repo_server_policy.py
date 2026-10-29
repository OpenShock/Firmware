"""Tests for scripts/repo_server_policy.py: which runs may publish to prod, with the GitHub API faked."""

import pytest
import repo_server_policy
import requests
from support import fails_with

from utils.github_api import GitHubError

ENV = {
    'REPO': 'OpenShock/Firmware',
    'SHA': 'a' * 40,
    'VERSION': '1.5.0',
    'GITHUB_TOKEN': 'token',
}


@pytest.fixture
def checks(monkeypatch):
    """Replaces the GitHub API checks with recorders, to test which ones main() runs."""
    calls = {'branch': [], 'draft': []}
    monkeypatch.setattr(
        repo_server_policy, 'require_branch_contains', lambda repo, channel, sha: calls['branch'].append(channel)
    )
    monkeypatch.setattr(
        repo_server_policy, 'require_staged_draft', lambda repo, version: calls['draft'].append(version)
    )
    for name, value in ENV.items():
        monkeypatch.setenv(name, value)
    return calls


def run(monkeypatch, server, channel):
    monkeypatch.setenv('SERVER', server)
    monkeypatch.setenv('CHANNEL', channel)
    return repo_server_policy.main()


def test_dev_server_skips_every_check(monkeypatch, checks):
    assert run(monkeypatch, 'dev', 'stable') == 0
    assert checks == {'branch': [], 'draft': []}


@pytest.mark.parametrize('channel', ['stable', 'beta'])
def test_prod_release_channels_need_branch_and_draft(monkeypatch, checks, channel):
    assert run(monkeypatch, 'prod', channel) == 0
    assert checks['branch'] == [channel]
    assert checks['draft'] == ['1.5.0']


def test_prod_develop_needs_the_develop_branch_but_no_draft(monkeypatch, checks):
    assert run(monkeypatch, 'prod', 'develop') == 0
    assert checks['branch'] == ['develop']
    assert checks['draft'] == []


def test_prod_unknown_channel_is_refused(monkeypatch, checks, capsys):
    with fails_with(capsys, 'Channel "feature-x" has no publishing rules for prod'):
        run(monkeypatch, 'prod', 'feature-x')
    assert checks['branch'] == []


# --- the checks themselves, against a faked GitHub API ---


@pytest.fixture
def github(monkeypatch):
    """Answers github_json() from a list of canned documents, one per call; records the paths asked for."""
    answers: list = []
    paths: list[str] = []

    def fake(path):
        paths.append(path)
        return answers.pop(0)

    monkeypatch.setattr(repo_server_policy, 'github_json', fake)
    monkeypatch.setattr(repo_server_policy.time, 'sleep', lambda seconds: None)
    return answers, paths


@pytest.mark.parametrize('status', ['identical', 'behind'])
def test_a_commit_contained_in_the_branch_may_publish(github, status):
    answers, paths = github
    answers.append({'status': status})
    repo_server_policy.require_branch_contains('OpenShock/Firmware', 'stable', 'abc')
    assert paths == ['/repos/OpenShock/Firmware/compare/master...abc']


@pytest.mark.parametrize('status', ['ahead', 'diverged'])
def test_a_commit_outside_the_branch_may_not_publish(github, capsys, status):
    answers, _ = github
    answers.append({'status': status})
    with fails_with(capsys, f'abc is not contained in beta \\(compare says {status}\\).*beta channel'):
        repo_server_policy.require_branch_contains('OpenShock/Firmware', 'beta', 'abc')


def test_a_draft_that_appears_while_polling_is_accepted(github):
    answers, paths = github
    answers += [[], [{'tag_name': '1.5.0', 'draft': True}]]
    repo_server_policy.require_staged_draft('OpenShock/Firmware', '1.5.0')
    assert len(paths) == 2


@pytest.mark.parametrize('release', [None, {'tag_name': '1.5.0', 'draft': False}])
def test_a_missing_or_already_published_release_is_refused(github, monkeypatch, capsys, release):
    answers, paths = github
    answers += [[release] if release else []] * repo_server_policy.DRAFT_ATTEMPTS
    slept = []
    monkeypatch.setattr(repo_server_policy.time, 'sleep', slept.append)
    with fails_with(capsys, 'No draft release for 1.5.0'):
        repo_server_policy.require_staged_draft('OpenShock/Firmware', '1.5.0')
    assert len(paths) == repo_server_policy.DRAFT_ATTEMPTS
    # No pointless wait after the last attempt.
    assert len(slept) == repo_server_policy.DRAFT_ATTEMPTS - 1


@pytest.mark.parametrize(
    'error, message',
    [
        (requests.ConnectionError('down'), 'Could not reach the GitHub API for /x: down'),
        (GitHubError(404, 'Not Found'), 'GitHub returned HTTP 404: Not Found for /x'),
    ],
)
def test_github_json_fails_naming_the_path(monkeypatch, capsys, error, message):
    def raise_error(path, token):
        raise error

    monkeypatch.setattr(repo_server_policy, 'github_get', raise_error)
    with fails_with(capsys, message):
        repo_server_policy.github_json('/x')
