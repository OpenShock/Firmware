import pytest

import repo_server_policy

ENV = {
    'REPO': 'OpenShock/Firmware',
    'SHA': 'a' * 40,
    'VERSION': '1.5.0',
    'GITHUB_TOKEN': 'token',
}


@pytest.fixture
def checks(monkeypatch):
    """Replaces the GitHub API checks with recorders."""
    calls = {'branch': [], 'draft': []}
    monkeypatch.setattr(repo_server_policy, 'require_branch_contains', lambda repo, channel, sha: calls['branch'].append(channel))
    monkeypatch.setattr(repo_server_policy, 'require_staged_draft', lambda repo, version: calls['draft'].append(version))
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


def test_prod_unknown_channel_is_refused(monkeypatch, checks):
    with pytest.raises(SystemExit):
        run(monkeypatch, 'prod', 'feature-x')
    assert checks['branch'] == []
