"""Tests for the command lines of the build scripts: build.py, gen_env_header.py and the --board option."""

import importlib.util

import gen_env_header
import pytest
from support import ROOT

from utils import boards, github

# Loaded from its path: a plain `import build` could resolve to PyPA's `build` package instead.
_spec = importlib.util.spec_from_file_location('openshock_build', ROOT / 'scripts' / 'build.py')
build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(build)


@pytest.fixture
def idf(monkeypatch, tmp_path):
    """Runs build.main without ESP-IDF: records the idf.py command instead of running it."""
    calls = []
    monkeypatch.setattr(build.subprocess, 'call', lambda cmd, env=None: calls.append(cmd) or 0)
    monkeypatch.setattr(build.shutil, 'which', lambda name: None)
    monkeypatch.setattr(build.pyreqs, 'check', lambda path=None: [])
    monkeypatch.setenv('IDF_PATH', str(tmp_path / 'idf'))
    monkeypatch.setenv('OPENSHOCK_BUILD_DIR', str(tmp_path / 'build'))
    return calls


def defaults_arg(cmd):
    return next(a for a in cmd if a.startswith('-DSDKCONFIG_DEFAULTS='))


def test_idf_runs_in_utf8_mode(monkeypatch, tmp_path):
    seen = {}
    monkeypatch.setattr(build.subprocess, 'call', lambda cmd, env=None: seen.update(env) or 0)
    monkeypatch.setattr(build.shutil, 'which', lambda name: None)
    monkeypatch.setenv('IDF_PATH', str(tmp_path / 'idf'))
    monkeypatch.delenv('PYTHONUTF8', raising=False)

    assert build.run_idf(['build']) == 0
    assert seen['PYTHONUTF8'] == '1'


def test_without_idf_path_an_installer_registered_idf_is_activated(monkeypatch, tmp_path):
    calls = []
    monkeypatch.setattr(build.subprocess, 'call', lambda cmd, env=None: calls.append(cmd) or 0)
    monkeypatch.setattr(build.shutil, 'which', lambda name: None)
    monkeypatch.delenv('IDF_PATH', raising=False)
    idf = tmp_path / 'idf'

    def fake_activate(environ=None):
        monkeypatch.setenv('IDF_PATH', str(idf))
        return 'venv-python'

    monkeypatch.setattr(build.idfenv, 'activate', fake_activate)

    assert build.run_idf(['build']) == 0
    assert calls[0][:2] == ['venv-python', str(idf / 'tools' / 'idf.py')]


def test_without_idf_path_or_an_install_the_error_names_the_export_script(monkeypatch):
    monkeypatch.setattr(build.shutil, 'which', lambda name: None)
    monkeypatch.delenv('IDF_PATH', raising=False)
    monkeypatch.setattr(build.idfenv, 'activate', lambda environ=None: None)

    with pytest.raises(SystemExit, match='export script'):
        build.run_idf(['build'])


def test_the_build_hands_its_own_interpreter_to_cmake(idf):
    assert build.main(['OpenShock-Core-V2']) == 0
    assert f'-DOPENSHOCK_PYTHON={build.sys.executable}' in idf[0]


def test_missing_requirements_stop_the_build_before_idf_runs(idf, monkeypatch):
    monkeypatch.setattr(build.pyreqs, 'check', lambda path=None: [build.pyreqs.Pin('littlefs-python', '0.19.0')])

    with pytest.raises(SystemExit, match=r'lacks 1 of the pins in requirements.txt: littlefs-python==0.19.0'):
        build.main(['OpenShock-Core-V2'])
    assert idf == []


def test_a_successful_build_publishes_its_compile_database(idf, monkeypatch, tmp_path):
    monkeypatch.setattr(build, 'ROOT', tmp_path)
    build_dir = tmp_path / 'build' / 'OpenShock-Core-V2'
    monkeypatch.setenv('OPENSHOCK_BUILD_DIR', str(build_dir))
    build_dir.mkdir(parents=True)
    (build_dir / 'compile_commands.json').write_text('[]', encoding='utf-8')

    assert build.main(['OpenShock-Core-V2']) == 0
    assert (tmp_path / 'build' / 'compile_commands.json').read_text(encoding='utf-8') == '[]'


def test_variant_mode_builds_the_variant_without_a_board(idf, tmp_path):
    variant = boards.group_variants(['OpenShock-Core-V2'])[0]

    assert build.main(['--variant', variant.id]) == 0

    (cmd,) = idf
    assert f'-DIDF_TARGET={variant.target}' in cmd and cmd[-1] == 'build'
    # Passed empty, so a board an earlier configure cached in the build directory is not stamped.
    assert '-DOPENSHOCK_BOARD=' in cmd
    fragment = tmp_path / 'build' / 'variant.defaults'
    # Outside the repository, the fragment is passed by its absolute path.
    assert defaults_arg(cmd) == f'-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;{fragment.as_posix()}'
    assert variant.sdkconfig_fragment() in fragment.read_text(encoding='utf-8')


def test_board_mode_strips_the_pins(idf, tmp_path):
    assert build.main(['OpenShock-Core-V2', 'app']) == 0

    (cmd,) = idf
    assert '-DIDF_TARGET=esp32s3' in cmd and '-DOPENSHOCK_BOARD=OpenShock-Core-V2' in cmd and cmd[-1] == 'app'
    text = (tmp_path / 'build' / 'board.defaults').read_text(encoding='utf-8')
    assert 'CONFIG_IDF_TARGET="esp32s3"' in text
    assert not any(line.startswith('OPENSHOCK_') for line in text.splitlines())


def test_a_build_dir_in_the_repository_is_passed_relative(idf, monkeypatch, tmp_path):
    # A relative build dir resolves against the repository root, so stand one in: pytest must not write into the checkout.
    monkeypatch.setattr(build, 'ROOT', tmp_path)
    monkeypatch.setenv('OPENSHOCK_BUILD_DIR', 'build/OpenShock-Core-V2')
    build.main(['OpenShock-Core-V2'])
    (cmd,) = idf
    assert defaults_arg(cmd) == '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;build/OpenShock-Core-V2/board.defaults'
    assert (tmp_path / 'build' / 'OpenShock-Core-V2' / 'board.defaults').is_file()


def test_unknown_variant_is_refused(idf):
    with pytest.raises(SystemExit, match='OpenShock-Core-V2'):
        build.main(['--variant', 'esp32s3-00000000'])
    assert idf == []


@pytest.mark.parametrize(
    'env, expected',
    [
        ({}, ('debug', 5)),
        ({'GITHUB_REF_NAME': 'develop'}, ('debug', 5)),
        ({'GITHUB_REF_NAME': 'master'}, ('release', 3)),
        ({'GITHUB_REF_NAME': 'beta'}, ('release', 4)),
        ({'GITHUB_EVENT_NAME': 'pull_request', 'GITHUB_BASE_REF': 'master'}, ('release', 3)),
        ({'GITHUB_EVENT_NAME': 'pull_request', 'GITHUB_BASE_REF': 'beta'}, ('release', 4)),
        ({'GITHUB_REF_NAME': '1.5.0', 'GITHUB_REF_TYPE': 'tag'}, ('release', 3)),
        ({'GITHUB_REF_NAME': '1.5.0-rc.1', 'GITHUB_REF_TYPE': 'tag'}, ('release', 4)),
        ({'GITHUB_REF_NAME': '1.5.0-dev.1', 'GITHUB_REF_TYPE': 'tag'}, ('release', 3)),
        ({'GITHUB_REF_NAME': '1.5.0-beta.2', 'GITHUB_REF_TYPE': 'tag'}, ('release', 4)),
        # A branch named like a channel is only that channel's when it is not a tag.
        ({'GITHUB_REF_NAME': 'beta', 'GITHUB_REF_TYPE': 'branch'}, ('release', 4)),
    ],
)
def test_build_mode_follows_the_ref(monkeypatch, env, expected):
    monkeypatch.setenv('CI', 'true')
    monkeypatch.setenv('GITHUB_ACTIONS', 'true')
    for name, value in env.items():
        monkeypatch.setenv(name, value)
    assert github.build_mode() == expected


def test_build_mode_outside_ci_is_debug(monkeypatch):
    monkeypatch.setenv('GITHUB_REF_NAME', 'master')
    assert github.build_mode() == ('debug', 5)


@pytest.mark.parametrize(
    'env, expected',
    [
        ({}, 'debug'),
        ({'GITHUB_REF_NAME': 'develop'}, 'debug'),
        ({'GITHUB_REF_NAME': 'beta'}, 'debug'),
        ({'GITHUB_REF_NAME': 'master'}, 'size'),
        ({'GITHUB_EVENT_NAME': 'pull_request', 'GITHUB_BASE_REF': 'master'}, 'size'),
        ({'GITHUB_EVENT_NAME': 'pull_request', 'GITHUB_BASE_REF': 'develop'}, 'debug'),
        ({'GITHUB_REF_NAME': '1.5.0', 'GITHUB_REF_TYPE': 'tag'}, 'size'),
        ({'GITHUB_REF_NAME': '1.5.0-rc.1', 'GITHUB_REF_TYPE': 'tag'}, 'debug'),
        ({'GITHUB_REF_NAME': '1.5.0-dev.1', 'GITHUB_REF_TYPE': 'tag'}, 'debug'),
    ],
)
def test_only_stable_builds_optimized(monkeypatch, env, expected):
    monkeypatch.setenv('CI', 'true')
    monkeypatch.setenv('GITHUB_ACTIONS', 'true')
    for name, value in env.items():
        monkeypatch.setenv(name, value)
    assert github.build_optimization() == expected


def test_optimization_outside_ci_is_debug(monkeypatch):
    monkeypatch.setenv('GITHUB_REF_NAME', 'master')
    assert github.build_optimization() == 'debug'


def test_board_fragment_selects_the_optimization(idf, tmp_path):
    assert build.main(['OpenShock-Core-V2', 'app']) == 0
    text = (tmp_path / 'build' / 'board.defaults').read_text(encoding='utf-8')
    assert text.splitlines()[-1] == 'CONFIG_COMPILER_OPTIMIZATION_DEBUG=y'


def test_stable_variant_compiles_for_size(idf, monkeypatch, tmp_path):
    monkeypatch.setenv('OPENSHOCK_FW_OPTIMIZATION', 'size')
    variant = boards.group_variants(['OpenShock-Core-V2'])[0]
    assert build.main(['--variant', variant.id]) == 0
    text = (tmp_path / 'build' / 'variant.defaults').read_text(encoding='utf-8')
    assert text.splitlines()[-1] == 'CONFIG_COMPILER_OPTIMIZATION_SIZE=y'
    assert 'CONFIG_COMPILER_OPTIMIZATION_DEBUG=y' not in text


def test_optimization_is_not_part_of_the_variant_id(monkeypatch):
    variant = boards.group_variants(['OpenShock-Core-V2'])[0]
    monkeypatch.setenv('OPENSHOCK_FW_OPTIMIZATION', 'size')
    assert boards.group_variants(['OpenShock-Core-V2'])[0].id == variant.id


def test_a_local_size_build_gets_its_own_directory(monkeypatch):
    # An existing sdkconfig wins over the defaults, so sharing build/<board> would keep whichever level ran first.
    monkeypatch.delenv('OPENSHOCK_BUILD_DIR', raising=False)
    assert build.resolve_build_dir('OpenShock-Core-V2') == ROOT / 'build' / 'OpenShock-Core-V2'
    assert build.resolve_build_dir('OpenShock-Core-V2', 'size') == ROOT / 'build' / 'OpenShock-Core-V2-size'


def test_unknown_optimization_is_refused(idf, monkeypatch):
    monkeypatch.setenv('OPENSHOCK_FW_OPTIMIZATION', 'O3')
    with pytest.raises(SystemExit, match='debug, size'):
        build.main(['OpenShock-Core-V2'])
    assert idf == []


@pytest.mark.parametrize(
    'ref, expected',
    [
        ('refs/tags/1.5.0', ('tag', '1.5.0')),
        ('refs/heads/feature/x', ('branch', 'feature/x')),
        ('refs/pull/12/merge', ('pull', '12')),
        ('refs/remotes/origin/x', ('', 'refs/remotes/origin/x')),
    ],
)
def test_parse_ref(ref, expected):
    parsed = github.parse_ref(ref)
    assert (parsed.kind, parsed.name) == expected


def test_gen_env_header_writes_both_headers_and_keeps_numeric_prereleases_strings(tmp_path, monkeypatch):
    monkeypatch.setenv('OPENSHOCK_FW_VERSION', '1.2.3-1+abc')
    monkeypatch.setattr(gen_env_header, 'git_commit', lambda: 'deadbeef')
    gen_env_header.main(['--out-dir', str(tmp_path), '--target', 'esp32s3'])
    assert '#define OPENSHOCK_FW_CHIP_ESP32S3 1' in (tmp_path / 'openshock_board.h').read_text(encoding='utf-8')
    version = (tmp_path / 'openshock_version.h').read_text(encoding='utf-8')
    assert '#define OPENSHOCK_FW_VERSION_PRERELEASE "1"' in version
    assert '#define OPENSHOCK_FW_VERSION_MAJOR 1' in version
    assert '#define OPENSHOCK_FW_GIT_COMMIT "deadbeef"' in version


def test_board_names_splits_and_refuses_repeats():
    assert boards.board_names(['A B', 'C']) == ['A', 'B', 'C']
    assert boards.board_names(['A,B\nC']) == ['A', 'B', 'C']
    assert boards.parse_board_list('A, B\n C,,') == ['A', 'B', 'C']
    with pytest.raises(SystemExit, match='no board names'):
        boards.board_names([' '])
    with pytest.raises(SystemExit, match='repeated board.*A'):
        boards.board_names(['A', 'B A'])
