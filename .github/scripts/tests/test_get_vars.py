import json

import get_vars
import pytest


@pytest.mark.parametrize(
    'tag, expected',
    [
        ('1.5.0', '1.5.0'),
        ('v0.8.1', '0.8.1'),  # historical v prefix
        ('1.5.0-rc.2', '1.5.0-rc.2'),
    ],
)
def test_parse_version_accepts(tag, expected):
    assert str(get_vars.parse_version(tag)) == expected


@pytest.mark.parametrize('tag', ['', 'latest', '1.5', 'v1.x.0'])
def test_parse_version_rejects(tag):
    assert get_vars.parse_version(tag) is None


@pytest.mark.parametrize(
    'tag, channel',
    [
        ('1.5.0', 'stable'),
        ('1.5.0-rc.1', 'beta'),
        ('1.5.0-beta.3', 'beta'),
        ('1.5.0-develop.4', 'develop'),
        ('1.5.0-dev.4', 'develop'),
        ('1.5.0-feature.1', None),
    ],
)
def test_channel_of_version(tag, channel):
    version = get_vars.parse_version(tag)
    assert get_vars.CHANNEL_BY_PRERELEASE.get(get_vars.prerelease_id(version)) == channel


@pytest.mark.parametrize(
    'name, expected',
    [
        ('feature/new-thing', 'feature-new-thing'),
        ('--weird__name--', 'weird-name'),
        ('a///b', 'a-b'),
    ],
)
def test_sanitize(name, expected):
    assert get_vars.sanitize(name) == expected


@pytest.fixture
def run_main(monkeypatch):
    """Runs get-vars main() for a ref and board selection, git's tag list stubbed out; returns its outputs."""
    monkeypatch.setattr(get_vars.subprocess, 'check_output', lambda *a, **kw: '1.4.0\n1.3.0\n')
    for name in ('GITHUB_HEAD_REF', 'RELEASE_NEXT_VERSION', 'RELEASE_SKIP', 'CHANNEL_INPUT', 'NIGHTLY_INPUT'):
        monkeypatch.delenv(name, raising=False)

    def run(ref, boards_input=''):
        outputs = {}
        monkeypatch.setattr(get_vars, 'set_output', lambda name, value: outputs.__setitem__(name, value))
        monkeypatch.setenv('GITHUB_REF', ref)
        monkeypatch.setenv('GITHUB_SHA', '0123456789abcdef')
        monkeypatch.setenv('BOARDS_INPUT', boards_input)
        assert get_vars.main() == 0
        return outputs, json.loads(outputs['variant-matrix'])['include']

    return run


def test_main_on_develop_builds_every_variant(run_main):
    outputs, entries = run_main('refs/heads/develop')
    assert outputs['release-channel'] == 'develop'
    assert outputs['version'] == '1.4.0-develop+01234567'
    assert len(entries) > 1
    assert sorted(b for e in entries for b in e['boards'].split()) == outputs['board-list'].split('\n')
    assert all(set(e) == {'variant', 'chip', 'boards'} for e in entries)


def test_main_narrowed_to_one_board_builds_its_variant_only(run_main):
    outputs, entries = run_main('refs/heads/develop', 'OpenShock-Core-V2')
    assert outputs['board-list'] == 'OpenShock-Core-V2'
    assert [(e['chip'], e['boards']) for e in entries] == [('esp32s3', 'OpenShock-Core-V2')]


@pytest.mark.parametrize(
    'tag, channel',
    [('1.5.0', 'stable'), ('1.5.0-rc.1', 'beta'), ('1.5.0-beta.2', 'beta'), ('1.5.0-dev.4', 'develop')],
)
def test_main_on_a_tag_releases_the_tag(run_main, tag, channel):
    outputs, _ = run_main(f'refs/tags/{tag}')
    assert (outputs['version'], outputs['release-channel']) == (tag, channel)


def test_main_on_a_feature_branch_publishes_to_develop(run_main):
    outputs, _ = run_main('refs/heads/feature/thing')
    assert outputs['release-channel'] == 'develop'


@pytest.mark.parametrize(
    'channel, nightly, expected',
    [('beta', 'false', 'beta'), ('derive', 'false', 'develop'), ('', '', 'develop'), ('beta', 'true', 'develop')],
)
def test_main_channel_input(run_main, monkeypatch, channel, nightly, expected):
    monkeypatch.setenv('CHANNEL_INPUT', channel)
    monkeypatch.setenv('NIGHTLY_INPUT', nightly)
    outputs, _ = run_main('refs/heads/develop')
    assert outputs['release-channel'] == expected
