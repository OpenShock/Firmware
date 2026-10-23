"""Tests for the installer-registered ESP-IDF discovery (scripts/utils/idfenv.py)."""

import json
import os

import pytest

from utils import idfenv


def test_env_dump_prepends_path_and_skips_system_path():
    env = {'PATH': 'mine'}
    dump = '\n'.join(
        [
            'PATH=' + os.pathsep.join(['a', 'b']),
            'SYSTEM_PATH=should-be-ignored',
            'IDF_PATH=/idf',
            'IDF_PYTHON_ENV_PATH=/venv',
            'not a pair',
        ]
    )
    idfenv.apply_env_dump(dump, env)
    assert env['PATH'] == os.pathsep.join(['a', 'b', 'mine'])
    assert env['IDF_PATH'] == '/idf'
    assert 'SYSTEM_PATH' not in env


def test_find_install_prefers_the_selected_entry(monkeypatch, tmp_path):
    meta = tmp_path / 'eim_idf.json'
    meta.write_text(
        json.dumps(
            {
                'idfSelectedId': 'two',
                'idfInstalled': [
                    {'id': 'one', 'name': 'v6.0', 'activationScript': 'one.sh'},
                    {'id': 'two', 'name': 'v6.1', 'activationScript': 'two.sh'},
                ],
            }
        )
    )
    monkeypatch.setattr(idfenv, '_candidate_metadata', lambda: [tmp_path / 'missing.json', meta])
    assert idfenv.find_install()['name'] == 'v6.1'


def test_find_install_without_metadata_is_none(monkeypatch, tmp_path):
    monkeypatch.setattr(idfenv, '_candidate_metadata', lambda: [tmp_path / 'missing.json'])
    assert idfenv.find_install() is None


def test_activate_leaves_an_exported_shell_alone(monkeypatch):
    env = {'IDF_PATH': '/already'}

    def must_not_look():
        pytest.fail('an exported shell must not trigger discovery')

    monkeypatch.setattr(idfenv, 'find_install', must_not_look)
    assert idfenv.activate(env)
    assert env == {'IDF_PATH': '/already'}


def test_activate_applies_the_dump_and_returns_the_venv_python(monkeypatch, tmp_path):
    python = tmp_path / 'python.exe'
    python.write_text('')
    install = {'name': 'v6.1', 'path': str(tmp_path), 'activationScript': 'act.ps1', 'python': str(python)}
    monkeypatch.setattr(idfenv, 'find_install', lambda: install)
    monkeypatch.setattr(idfenv, 'env_dump', lambda script: f'IDF_PATH={tmp_path}\nPATH=tools')
    env = {'PATH': 'mine'}

    assert idfenv.activate(env) == str(python)
    assert env['IDF_PATH'] == str(tmp_path)
    assert env['PATH'].startswith('tools' + os.pathsep)
