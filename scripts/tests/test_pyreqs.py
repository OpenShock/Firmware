"""Tests for the requirements check the build runs on its interpreter (scripts/utils/pyreqs.py)."""

import pytest

from utils import pyreqs

PINNED = """\
# compiled by uv
cffi==2.1.1 ; platform_python_implementation != 'PyPy' \\
    --hash=sha256:aaaa \\
    --hash=sha256:bbbb
    # via cryptography
colorama==0.4.6 ; sys_platform == 'win32' \\
    --hash=sha256:cccc
littlefs-python==0.19.0 \\
    --hash=sha256:dddd
pycparser==3.11 ; python_full_version < '3.11' and implementation_name != 'PyPy' \\
    --hash=sha256:eeee
pycparser==3.12 ; python_full_version >= '3.11' and implementation_name != 'PyPy' \\
    --hash=sha256:ffff
pyserial==3.5
"""


def test_parse_pins_reads_name_version_and_marker_and_skips_hash_lines():
    pins = pyreqs.parse_pins(PINNED)
    assert [str(p) for p in pins] == [
        'cffi==2.1.1',
        'colorama==0.4.6',
        'littlefs-python==0.19.0',
        'pycparser==3.11',
        'pycparser==3.12',
        'pyserial==3.5',
    ]
    assert pins[0].marker == "platform_python_implementation != 'PyPy'"
    assert pins[2].marker is None
    assert pins[5].marker is None


def test_missing_pins_honours_markers_and_versions(monkeypatch):
    installed = {'cffi': '2.1.1', 'littlefs-python': '0.18.0', 'pycparser': '3.12', 'pyserial': '3.5'}
    monkeypatch.setattr(pyreqs, 'installed_version', installed.get)
    # Pretend: CPython 3.12 on Linux, so colorama and pycparser 3.11 do not apply.
    holds = {
        "platform_python_implementation != 'PyPy'": True,
        "sys_platform == 'win32'": False,
        "python_full_version < '3.11' and implementation_name != 'PyPy'": False,
        "python_full_version >= '3.11' and implementation_name != 'PyPy'": True,
    }
    monkeypatch.setattr(pyreqs, 'marker_holds', holds.__getitem__)

    missing = pyreqs.missing_pins(pyreqs.parse_pins(PINNED))
    assert [str(p) for p in missing] == ['littlefs-python==0.19.0']


def test_marker_evaluation_uses_the_real_environment():
    assert pyreqs.marker_holds("python_version >= '3.0'") is True
    assert pyreqs.marker_holds("python_version < '3.0'") is False


def test_check_against_the_real_requirements_names_only_absent_pins(tmp_path, monkeypatch):
    # pytest itself is installed here, at whatever version; a bogus version must be reported, a real one not.
    import importlib.metadata as m

    real = m.version('pytest')
    (tmp_path / 'req.txt').write_text(f'pytest=={real}\npytest-nonexistent-plugin==1.0\n', encoding='utf-8')
    missing = pyreqs.check(tmp_path / 'req.txt')
    assert [str(p) for p in missing] == ['pytest-nonexistent-plugin==1.0']


def test_report_names_the_interpreter_the_count_and_the_pins():
    msg = pyreqs.report([pyreqs.Pin('a', '1'), pyreqs.Pin('b', '2')], pyreqs.REQUIREMENTS, python='py')
    assert msg.startswith('error: py lacks 2 of the pins in requirements.txt: a==1, b==2')
    assert 'ESP-IDF venv' in msg


def test_main_exits_nonzero_with_the_report(tmp_path, capsys):
    (tmp_path / 'req.txt').write_text('pytest-nonexistent-plugin==1.0\n', encoding='utf-8')
    assert pyreqs.main(['--check', str(tmp_path / 'req.txt')]) == 1
    assert 'pytest-nonexistent-plugin==1.0' in capsys.readouterr().err
    (tmp_path / 'ok.txt').write_text('# nothing pinned\n', encoding='utf-8')
    assert pyreqs.main(['--check', str(tmp_path / 'ok.txt')]) == 0


def test_main_requires_a_file(capsys):
    with pytest.raises(SystemExit) as exc:
        pyreqs.main([])
    assert exc.value.code == 2  # argparse's usage error
    assert '--check' in capsys.readouterr().err
