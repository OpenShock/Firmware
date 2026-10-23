"""CMakePresets.json is generated from boards/*.defaults and committed; it must stay current."""

import json

import gen_cmake_presets
from support import ROOT

from utils.boards import all_boards


def test_committed_presets_are_current():
    assert gen_cmake_presets.main(['--check']) == 0, 'run python scripts/gen_cmake_presets.py'


def test_every_board_has_a_configure_and_a_build_preset():
    data = json.loads((ROOT / 'CMakePresets.json').read_text(encoding='utf-8'))
    configure = {p['name']: p for p in data['configurePresets'] if not p.get('hidden')}
    build = {p['configurePreset'] for p in data['buildPresets']}
    assert set(configure) == set(all_boards()) == build
    for name, preset in configure.items():
        assert preset['cacheVariables'] == {'OPENSHOCK_BOARD': name}


def test_check_fails_on_a_stale_file(tmp_path):
    stale = tmp_path / 'CMakePresets.json'
    stale.write_text('{}')
    assert gen_cmake_presets.main(['--check', '--output', str(stale)]) == 1
    assert gen_cmake_presets.main(['--output', str(stale)]) == 0
    assert gen_cmake_presets.main(['--check', '--output', str(stale)]) == 0
