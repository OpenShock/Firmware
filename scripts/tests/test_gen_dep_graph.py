"""Tests for scripts/gen_dep_graph.py: reading idf_component_register() and rendering the Mermaid graph."""

import gen_dep_graph
import pytest

CMAKE = """
idf_component_register(
    SRCS "a.cpp" # REQUIRES in a comment is not a keyword
    INCLUDE_DIRS "include"
    REQUIRES fs $<IF:$<BOOL:x>,y,z> util
    PRIV_REQUIRES esp_http_server
)
"""


def test_parse_requires_reads_public_and_private_lists():
    assert gen_dep_graph.parse_requires(CMAKE, include_priv=True) == [
        'fs',
        '$<IF:$<BOOL:x>,y,z>',
        'util',
        'esp_http_server',
    ]
    assert gen_dep_graph.parse_requires(CMAKE, include_priv=False) == ['fs', '$<IF:$<BOOL:x>,y,z>', 'util']


def test_parse_requires_without_a_register_call_is_empty():
    assert gen_dep_graph.parse_requires('project(x)\n', include_priv=True) == []


def test_extract_register_block_balances_nested_parens():
    text = 'idf_component_register(SRCS $(a) REQUIRES b) other(c)'
    assert gen_dep_graph.extract_register_block(text) == 'SRCS $(a) REQUIRES b'


@pytest.mark.parametrize('index, expected', [(0, 'A'), (25, 'Z'), (26, 'AA'), (27, 'AB'), (701, 'ZZ'), (702, 'AAA')])
def test_col_id_counts_like_spreadsheet_columns(index, expected):
    assert gen_dep_graph.col_id(index) == expected


def test_topo_order_puts_dependencies_first_with_an_alphabetical_tie_break():
    edges = {('util', 'rf'), ('util', 'fs'), ('fs', 'http')}
    assert gen_dep_graph.topo_order({'http', 'fs', 'rf', 'util'}, edges) == ['util', 'fs', 'http', 'rf']


def test_topo_order_appends_a_cycle_so_the_order_stays_total():
    order = gen_dep_graph.topo_order({'a', 'b', 'c'}, {('a', 'b'), ('b', 'a')})
    assert order == ['c', 'a', 'b']


def test_build_graph_drops_external_dependencies_unless_asked():
    components = {'rf': ['util', 'driver'], 'util': []}
    assert gen_dep_graph.build_graph(components, external=False) == ({'rf', 'util'}, {('util', 'rf')})
    nodes, edges = gen_dep_graph.build_graph(components, external=True)
    assert nodes == {'rf', 'util', 'driver'}
    assert edges == {('util', 'rf'), ('driver', 'rf')}


def test_render_mermaid_labels_and_orders_nodes():
    text = gen_dep_graph.render_mermaid({'osjson', 'led_drivers'}, {('osjson', 'led_drivers')}, 'LR')
    assert text.splitlines() == ['flowchart LR', '    A(OSJson)', '    B(LedDrivers)', '    A --> B']


def test_main_writes_the_graph_of_a_components_dir(tmp_path, capsys):
    for name, body in {'fs': 'idf_component_register(SRCS "x")', 'rf': 'idf_component_register(REQUIRES fs)'}.items():
        (tmp_path / name).mkdir()
        (tmp_path / name / 'CMakeLists.txt').write_text(body, encoding='utf-8')
    out = tmp_path / 'graph.mmd'
    assert gen_dep_graph.main(['--components-dir', str(tmp_path), '-o', str(out)]) == 0
    assert out.read_text(encoding='utf-8') == 'flowchart TD\n    A(FS)\n    B(Rf)\n    A --> B\n'
    assert 'wrote' in capsys.readouterr().err


def test_main_refuses_a_missing_components_dir(tmp_path):
    with pytest.raises(SystemExit, match='is not a directory'):
        gen_dep_graph.main(['--components-dir', str(tmp_path / 'nope')])
