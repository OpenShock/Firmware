import importlib.util
from pathlib import Path

import pytest

# get-vars.py has a hyphen in its name, so it can't be imported with a plain import statement.
_spec = importlib.util.spec_from_file_location('get_vars', Path(__file__).resolve().parent.parent / 'get-vars.py')
get_vars = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(get_vars)


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
    'tag, stable, beta, dev',
    [
        ('1.5.0', True, False, False),
        ('1.5.0-rc.1', False, True, False),
        ('1.5.0-beta.3', False, True, False),
        ('1.5.0-develop.4', False, False, True),
        ('1.5.0-dev.4', False, False, True),
        ('1.5.0-feature.1', False, False, False),
    ],
)
def test_channel_of_version(tag, stable, beta, dev):
    version = get_vars.parse_version(tag)
    assert get_vars.is_stable(version) is stable
    assert get_vars.is_beta(version) is beta
    assert get_vars.is_dev(version) is dev


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


def test_sdkconfig_lines_keeps_settings_and_disabled_bools_only(tmp_path):
    fragment = tmp_path / 'board.defaults'
    fragment.write_text(
        '# A comment\n'
        '\n'
        'CONFIG_IDF_TARGET="esp32"\n'
        'OPENSHOCK_RF_TX_GPIO=15\n'
        '# CONFIG_FOO is not set\n'
        'CONFIG_BAR=y\n',
        encoding='utf-8',
    )
    assert get_vars.sdkconfig_lines(fragment) == sorted(['CONFIG_IDF_TARGET="esp32"', '# CONFIG_FOO is not set', 'CONFIG_BAR=y'])


def test_cache_group_ignores_pins_but_not_disabled_bools(tmp_path):
    shared = tmp_path / 'sdkconfig.defaults'
    shared.write_text('CONFIG_X=y\n', encoding='utf-8')

    def group(content):
        fragment = tmp_path / 'board.defaults'
        fragment.write_text(content, encoding='utf-8')
        return get_vars.cache_group(fragment, 'esp32', shared)

    base = group('CONFIG_IDF_TARGET="esp32"\n')
    assert group('CONFIG_IDF_TARGET="esp32"\nOPENSHOCK_LED_GPIO=2\n') == base
    assert group('CONFIG_IDF_TARGET="esp32"\n# CONFIG_X is not set\n') != base
    assert base.startswith('esp32-')
