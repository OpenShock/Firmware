"""Tests for the installer-registered ESP-IDF discovery (scripts/utils/idfenv.py)."""

import json
import os
import subprocess

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
        ),
        encoding='utf-8',
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
    python.write_text('', encoding='utf-8')
    install = {'name': 'v6.1', 'path': str(tmp_path), 'activationScript': 'act.ps1', 'python': str(python)}
    monkeypatch.setattr(idfenv, 'find_install', lambda: install)
    monkeypatch.setattr(idfenv, 'env_dump', lambda script: f'IDF_PATH={tmp_path}\nPATH=tools')
    env = {'PATH': 'mine'}

    assert idfenv.activate(env) == str(python)
    assert env['IDF_PATH'] == str(tmp_path)
    assert env['PATH'].startswith('tools' + os.pathsep)


def test_a_corrupt_metadata_file_is_named(monkeypatch, tmp_path):
    meta = tmp_path / 'eim_idf.json'
    meta.write_text('{not json', encoding='utf-8')
    monkeypatch.setattr(idfenv, '_candidate_metadata', lambda: [meta])
    with pytest.raises(SystemExit, match='cannot read the ESP-IDF install metadata .*eim_idf.json'):
        idfenv.find_install()


def test_a_failing_activation_script_shows_its_output(monkeypatch):
    def fail(cmd, **kwargs):
        raise subprocess.CalledProcessError(1, cmd, output='', stderr='python venv is missing')

    monkeypatch.setattr(idfenv.subprocess, 'run', fail)
    with pytest.raises(SystemExit, match=r'activating ESP-IDF with act.sh failed \(exit 1\):\npython venv is missing'):
        idfenv.env_dump('act.sh')


def test_a_missing_shell_is_named(monkeypatch):
    def missing(cmd, **kwargs):
        raise FileNotFoundError(2, 'No such file', cmd[0])

    monkeypatch.setattr(idfenv.subprocess, 'run', missing)
    with pytest.raises(SystemExit, match='cannot run sh to activate ESP-IDF with act.sh'):
        idfenv.env_dump('act.sh')


def test_dump_prints_only_what_activation_changed(monkeypatch, capsys):
    monkeypatch.delenv('IDF_PATH', raising=False)
    monkeypatch.setenv('UNCHANGED', 'same')

    def fake_activate(environ):
        environ['IDF_PATH'] = '/idf'
        return '/venv/python'

    monkeypatch.setattr(idfenv, 'activate', fake_activate)
    assert idfenv.main(['--dump']) == 0
    lines = capsys.readouterr().out.splitlines()
    assert lines == ['IDF_PATH=/idf', 'PYTHON=/venv/python']


def test_dump_without_an_install_fails(monkeypatch, capsys):
    monkeypatch.setattr(idfenv, 'activate', lambda environ: None)
    assert idfenv.main(['--dump']) == 1
    assert 'no ESP-IDF install found' in capsys.readouterr().err
