"""Tests for the pure helpers of scripts/gen_env_header.py: version parsing and C header rendering."""

import gen_env_header
import pytest


@pytest.mark.parametrize(
    'version, expected',
    [
        ('1.5.0', (1, 5, 0, None, None)),
        ('v0.8.1', (0, 8, 1, None, None)),
        ('1.5.0-rc.2+abc123', (1, 5, 0, 'rc.2', 'abc123')),
        ('0.0.0-local+1234567', (0, 0, 0, 'local', '1234567')),
    ],
)
def test_split_semver(version, expected):
    assert gen_env_header.split_semver(version) == expected


@pytest.mark.parametrize('version', ['1.5', '1.5.0-', 'latest', ''])
def test_split_semver_rejects(version):
    with pytest.raises(ValueError, match='Invalid semver'):
        gen_env_header.split_semver(version)


def test_serialize_quotes_strings_even_when_numeric():
    assert gen_env_header.serialize(5) == '5'
    assert gen_env_header.serialize('1') == '"1"'
    assert gen_env_header.serialize('a"b\\c') == '"a\\"b\\\\c"'


def test_version_defines_include_only_the_parts_present(monkeypatch):
    monkeypatch.setenv('OPENSHOCK_FW_VERSION', '1.5.0-rc.2+abc')
    defines = gen_env_header.version_defines('deadbeef')
    assert defines['OPENSHOCK_FW_VERSION_MAJOR'] == 1
    assert defines['OPENSHOCK_FW_VERSION_PRERELEASE'] == 'rc.2'
    assert defines['OPENSHOCK_FW_VERSION_BUILD'] == 'abc'
    assert defines['OPENSHOCK_FW_GIT_COMMIT'] == 'deadbeef'

    monkeypatch.setenv('OPENSHOCK_FW_VERSION', '1.5.0')
    defines = gen_env_header.version_defines(None)
    assert not {'OPENSHOCK_FW_VERSION_PRERELEASE', 'OPENSHOCK_FW_VERSION_BUILD', 'OPENSHOCK_FW_GIT_COMMIT'} & set(defines)


def test_resolve_version_falls_back_locally_but_not_in_ci(monkeypatch):
    monkeypatch.delenv('OPENSHOCK_FW_VERSION', raising=False)
    monkeypatch.setattr(gen_env_header.github, 'is_ci', lambda: False)
    assert gen_env_header.resolve_version('0123456789') == '0.0.0-local+0123456'
    assert gen_env_header.resolve_version(None) == '0.0.0-local'

    monkeypatch.setattr(gen_env_header.github, 'is_ci', lambda: True)
    with pytest.raises(SystemExit, match='OPENSHOCK_FW_VERSION must be set'):
        gen_env_header.resolve_version(None)


def test_board_defines_name_the_chip_macro(monkeypatch):
    monkeypatch.setattr(gen_env_header.github, 'build_mode', lambda: ('release', 3))
    defines = gen_env_header.board_defines('ESP32-S3')
    assert defines['OPENSHOCK_FW_CHIP_ESP32S3'] == 1
    assert (defines['OPENSHOCK_FW_MODE'], defines['OPENSHOCK_LOG_LEVEL']) == ('release', 3)


def test_render_sorts_defines_under_a_pragma_once():
    text = gen_env_header.render('Title.', {'B_VALUE': 'x', 'A_VALUE': 1})
    assert text.splitlines()[2:] == ['#pragma once', '', '#define A_VALUE 1', '#define B_VALUE "x"']
