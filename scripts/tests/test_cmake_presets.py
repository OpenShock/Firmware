"""CMakePresets.json is generated from boards/*.defaults and committed; it must stay current."""

import json

import gen_cmake_presets
from support import ROOT

from utils.boards import all_boards


def test_committed_presets_are_current():
    assert gen_cmake_presets.main(['--check']) == 0, 'run python scripts/gen_cmake_presets.py'


def load_presets() -> dict:
    return json.loads((ROOT / 'CMakePresets.json').read_text(encoding='utf-8'))


def test_every_board_has_a_configure_and_a_build_preset():
    data = load_presets()
    configure = {p['name']: p for p in data['configurePresets'] if p['name'] != gen_cmake_presets.DEFAULT_PRESET}
    build = {p['configurePreset'] for p in data['buildPresets']}
    assert set(configure) == set(all_boards()) == build
    for name, preset in configure.items():
        assert preset['cacheVariables'] == {'OPENSHOCK_BOARD': name}
        assert preset['inherits'] == gen_cmake_presets.DEFAULT_PRESET


def test_default_preset_is_visible_and_passes_no_board():
    # idf.py 6.1 configures with the preset named 'default' when given none, else the first in the file,
    # and CMake rejects a hidden one. scripts/build.py would stop working if this preset went hidden,
    # was renamed, or set a board under the -D arguments the script passes.
    data = load_presets()
    assert gen_cmake_presets.DEFAULT_PRESET == 'default'
    first = data['configurePresets'][0]
    assert first['name'] == 'default'
    assert not first.get('hidden')
    assert 'cacheVariables' not in first
    assert 'inherits' not in first
    assert first['generator'] == 'Ninja'


def test_check_fails_on_a_stale_file(tmp_path):
    stale = tmp_path / 'CMakePresets.json'
    stale.write_text('{}', encoding='utf-8')
    assert gen_cmake_presets.main(['--check', '--output', str(stale)]) == 1
    assert gen_cmake_presets.main(['--output', str(stale)]) == 0
    assert gen_cmake_presets.main(['--check', '--output', str(stale)]) == 0
