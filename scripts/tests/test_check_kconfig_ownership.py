"""Tests for scripts/check_kconfig_ownership.py: which component may read which CONFIG_OPENSHOCK_* option."""

from pathlib import Path

import check_kconfig_ownership


def component(root: Path, name: str, *, requires: str = '', priv: str = '', kconfig: str = '', source: str = ''):
    path = root / name
    (path / 'src').mkdir(parents=True)
    (path / 'CMakeLists.txt').write_text(
        f'idf_component_register(SRCS "src/a.cpp" REQUIRES {requires} PRIV_REQUIRES {priv})\n', encoding='utf-8'
    )
    if kconfig:
        (path / 'Kconfig').write_text(kconfig, encoding='utf-8')
    (path / 'src' / 'a.cpp').write_text(source, encoding='utf-8')


def violations(root: Path) -> list[tuple[str, str, str | None]]:
    return [(name, option, owner) for name, _, option, owner in check_kconfig_ownership.find_violations(root)]


def test_own_and_required_options_are_allowed(tmp_path):
    component(tmp_path, 'base', kconfig='config OPENSHOCK_BASE\n    int "x"\n')
    component(tmp_path, 'mid', requires='base', kconfig='config OPENSHOCK_MID\n    int "x"\n')
    component(tmp_path, 'top', priv='mid', source='CONFIG_OPENSHOCK_MID CONFIG_OPENSHOCK_BASE')
    assert violations(tmp_path) == []


def test_a_private_requirement_does_not_export_its_options(tmp_path):
    component(tmp_path, 'base', kconfig='config OPENSHOCK_BASE\n    int "x"\n')
    component(tmp_path, 'mid', priv='base')
    component(tmp_path, 'top', requires='mid', source='CONFIG_OPENSHOCK_BASE')
    assert violations(tmp_path) == [('top', 'OPENSHOCK_BASE', 'base')]


def test_reading_a_component_above_is_a_violation(tmp_path):
    component(tmp_path, 'portal', requires='core', kconfig='config OPENSHOCK_PORTAL_SIZE\n    int "x"\n')
    component(tmp_path, 'core', source='CONFIG_OPENSHOCK_PORTAL_SIZE')
    assert violations(tmp_path) == [('core', 'OPENSHOCK_PORTAL_SIZE', 'portal')]


def test_an_undefined_option_is_a_violation(tmp_path):
    component(tmp_path, 'top', source='CONFIG_OPENSHOCK_NOWHERE')
    assert violations(tmp_path) == [('top', 'OPENSHOCK_NOWHERE', None)]


def test_a_choice_name_is_not_a_macro(tmp_path):
    kconfig = 'choice OPENSHOCK_LEVEL\n    prompt "x"\n    config OPENSHOCK_LEVEL_LOW\n        bool "low"\nendchoice\n'
    component(tmp_path, 'log', kconfig=kconfig, source='CONFIG_OPENSHOCK_LEVEL_LOW CONFIG_OPENSHOCK_LEVEL')
    assert violations(tmp_path) == [('log', 'OPENSHOCK_LEVEL', None)]


def test_host_tests_are_skipped(tmp_path):
    component(tmp_path, 'top')
    (tmp_path / 'top' / 'host_test').mkdir()
    (tmp_path / 'top' / 'host_test' / 't.cpp').write_text('CONFIG_OPENSHOCK_NOWHERE', encoding='utf-8')
    assert violations(tmp_path) == []


def test_a_quoted_requirement_counts(tmp_path):
    component(tmp_path, 'base', kconfig='config OPENSHOCK_BASE\n    int "x"\n')
    component(tmp_path, 'top', priv='"base"', source='CONFIG_OPENSHOCK_BASE')
    assert violations(tmp_path) == []


def test_the_repository_has_no_violations():
    assert check_kconfig_ownership.find_violations(check_kconfig_ownership.ROOT / 'components') == []
