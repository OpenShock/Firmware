"""Tests for scripts/get_vars.py: the version, channel and build-variant matrix a CI run derives from its ref."""

import json

import get_vars
import pytest
from support import fails_with

from utils import channels


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
    ],
)
def test_a_tag_publishes_to_the_channel_its_prerelease_names(tag, channel):
    assert get_vars.resolve_channel(True, tag, get_vars.parse_version(tag)) == channel


def test_a_tag_with_an_unknown_prerelease_is_refused(capsys):
    with fails_with(capsys, 'Tag "1.5.0-feature.1" has an unrecognized prerelease channel'):
        get_vars.resolve_channel(True, '1.5.0-feature.1', get_vars.parse_version('1.5.0-feature.1'))


def test_every_prerelease_channel_is_a_server_channel():
    assert set(channels.PRERELEASE_CHANNEL.values()) <= set(channels.CHANNELS)
    assert set(channels.BRANCH_CHANNEL.values()) == set(channels.CHANNELS)


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


def test_release_versions_skip_tags_that_are_not_versions():
    versions = get_vars.release_versions(['1.5.0', 'latest', 'v0.8.1', 'nightly-2026'])
    assert [str(v) for v in versions] == ['1.5.0', '0.8.1']


@pytest.fixture
def run_main(monkeypatch):
    """Runs get-vars main() for a ref and board selection, git's tag list stubbed out; returns its outputs."""
    monkeypatch.setattr(get_vars.subprocess, 'check_output', lambda *a, **kw: '1.4.0\n1.3.0\n')

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


@pytest.mark.parametrize('channel, expected', [('beta', 'beta'), ('derive', 'develop'), ('', 'develop')])
def test_main_channel_input(run_main, monkeypatch, channel, expected):
    monkeypatch.setenv('CHANNEL_INPUT', channel)
    outputs, _ = run_main('refs/heads/develop')
    assert outputs['release-channel'] == expected


def test_main_on_a_pull_request_names_the_version_after_the_head_branch(run_main, monkeypatch):
    monkeypatch.setenv('GITHUB_HEAD_REF', 'feature/rf-fix')
    outputs, _ = run_main('refs/pull/12/merge')
    assert outputs['version'] == '1.4.0-rf-fix+01234567'
    assert outputs['release-channel'] == 'develop'


@pytest.mark.parametrize('ref', ['refs/remotes/origin/x', 'develop'])
def test_main_refuses_a_ref_that_is_not_a_branch_tag_or_pull_request(run_main, capsys, ref):
    with fails_with(capsys, 'is not a valid branch, tag or pull request'):
        run_main(ref)


def test_main_refuses_a_pull_request_without_its_head_branch(run_main, capsys):
    with fails_with(capsys, 'Failed to get git head ref name'):
        run_main('refs/pull/12/merge')


def test_main_refuses_a_tag_that_is_not_a_version(run_main, capsys):
    with fails_with(capsys, 'Git tag "latest" is not a valid semver version'):
        run_main('refs/tags/latest')


@pytest.mark.parametrize(
    'ref, skip, expected',
    [
        ('refs/heads/develop', '', '1.5.0-develop+01234567'),
        ('refs/heads/develop', 'true', '1.4.0-develop+01234567'),
        ('refs/tags/1.4.1', '', '1.4.1'),
    ],
)
def test_main_next_version_applies_to_branch_builds_only(run_main, monkeypatch, ref, skip, expected):
    monkeypatch.setenv('RELEASE_NEXT_VERSION', '1.5.0')
    monkeypatch.setenv('RELEASE_SKIP', skip)
    outputs, _ = run_main(ref)
    assert outputs['version'] == expected


def test_main_refuses_a_channel_the_server_does_not_know(run_main, monkeypatch, capsys):
    monkeypatch.setenv('CHANNEL_INPUT', 'nightly')
    with fails_with(capsys, 'Channel "nightly" is not one of: stable, beta, develop'):
        run_main('refs/heads/develop')


def test_main_refuses_an_unknown_board(run_main, capsys):
    with fails_with(capsys, r'Unknown board\(s\) requested: No-Such-Board'):
        run_main('refs/heads/develop', 'OpenShock-Core-V2, No-Such-Board')
