#!/usr/bin/env python3
"""Generate a Mermaid dependency graph from the ESP-IDF component manifests.

Scans every `components/<name>/CMakeLists.txt`, parses the REQUIRES and
PRIV_REQUIRES lists out of its idf_component_register() call, and emits a Mermaid
`flowchart` where each edge points from a dependency to the component that needs
it (i.e. `X --> Y` reads "Y requires X").

By default only edges between local components (those with a directory under
components/) are drawn. Pass --external to also include external components
(IDF built-ins, managed_components, etc.) as nodes.

Usage:
    python scripts/gen_dep_graph.py                 # local components only
    python scripts/gen_dep_graph.py --external      # include IDF/managed deps
    python scripts/gen_dep_graph.py -o graph.mmd    # write to a file

Paste the output into any Mermaid renderer (GitHub, mermaid.live, VS Code).
"""

import argparse
import heapq
import re
import sys
from pathlib import Path

from utils.paths import ROOT

# idf_component_register() argument keywords. A token equal to one of these ends
# the current value list and (for REQUIRES/PRIV_REQUIRES) starts a new one.
CMAKE_KEYWORDS = {
    'SRCS',
    'SRC_DIRS',
    'EXCLUDE_SRCS',
    'INCLUDE_DIRS',
    'PRIV_INCLUDE_DIRS',
    'REQUIRES',
    'PRIV_REQUIRES',
    'REQUIRED_IDF_TARGETS',
    'EMBED_FILES',
    'EMBED_TXTFILES',
    'LDFRAGMENTS',
    'KCONFIG',
    'KCONFIG_PROJBUILD',
    'WHOLE_ARCHIVE',
}

# Component name -> display label. Anything not listed falls back to PascalCase
# of the snake_case directory name (e.g. led_drivers -> LedDrivers).
LABEL_OVERRIDES = {
    'osjson': 'OSJson',
    'http': 'HTTP',
    'fs': 'FS',
    'dns_server': 'DnsServer',
    'rfc8908': 'RFC8908',
    'hwutil': 'HwUtil',
}


def label_for(name: str) -> str:
    if name in LABEL_OVERRIDES:
        return LABEL_OVERRIDES[name]
    return ''.join(word.capitalize() for word in name.split('_'))


def col_id(index: int) -> str:
    """Spreadsheet-style id: 0->A, 25->Z, 26->AA, ..."""
    s = ''
    index += 1
    while index > 0:
        index, rem = divmod(index - 1, 26)
        s = chr(ord('A') + rem) + s
    return s


def strip_comments(text: str) -> str:
    return '\n'.join(line.split('#', 1)[0] for line in text.splitlines())


def extract_register_block(text: str) -> str:
    """Return the contents inside the idf_component_register( ... ) call."""
    m = re.search(r'idf_component_register\s*\(', text)
    if not m:
        return ''
    depth = 0
    start = m.end() - 1  # at the '('
    for i in range(start, len(text)):
        c = text[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return text[start + 1 : i]
    return text[start + 1 :]  # unbalanced; take the rest


def parse_requires(cmake_text: str, include_priv: bool) -> list[str]:
    """Pull the REQUIRES (+ optionally PRIV_REQUIRES) token lists."""
    block = extract_register_block(strip_comments(cmake_text))
    tokens = [tok.strip('"') for tok in block.split()]  # CMake reads "common" and common alike

    deps: list[str] = []
    active = False
    for tok in tokens:
        if tok in CMAKE_KEYWORDS:
            active = tok == 'REQUIRES' or (tok == 'PRIV_REQUIRES' and include_priv)
            continue
        if active:
            deps.append(tok)

    return list(dict.fromkeys(deps))


def topo_order(nodes: set[str], edges: set[tuple[str, str]]) -> list[str]:
    """Dependencies first. Edge (dep, dependent). Alphabetical tie-break; any
    nodes left in a cycle are appended in sorted order so output stays total."""
    indeg = {n: 0 for n in nodes}
    adj: dict[str, list[str]] = {n: [] for n in nodes}
    for dep, dependent in edges:
        adj[dep].append(dependent)
        indeg[dependent] += 1

    ready = [n for n in nodes if indeg[n] == 0]
    heapq.heapify(ready)
    order = []
    while ready:
        n = heapq.heappop(ready)
        order.append(n)
        for m in sorted(adj[n]):
            indeg[m] -= 1
            if indeg[m] == 0:
                heapq.heappush(ready, m)

    if len(order) < len(nodes):
        order.extend(sorted(nodes - set(order)))
    return order


Edge = tuple[str, str]  # (dependency, dependent)


def build_graph(components: dict[str, list[str]], *, external: bool) -> tuple[set[str], set[Edge]]:
    """Nodes and (dependency, dependent) edges for components given as name -> required components.

    A dependency that is not itself a component is dropped unless `external` is set.
    """
    nodes: set[str] = set(components)
    edges: set[Edge] = set()
    for name, deps in components.items():
        for dep in deps:
            if dep not in components and not external:
                continue
            edges.add((dep, name))
            nodes.add(dep)
    return nodes, edges


def render_mermaid(nodes: set[str], edges: set[Edge], direction: str) -> str:
    """A Mermaid flowchart: dependencies first, edges grouped by source in node order for stable output."""
    order = topo_order(nodes, edges)
    ids = {name: col_id(i) for i, name in enumerate(order)}
    lines = [f'flowchart {direction}']
    lines += [f'    {ids[name]}({label_for(name)})' for name in order]
    lines += [f'    {ids[src]} --> {ids[dst]}' for src in order for dst in order if (src, dst) in edges]
    return '\n'.join(lines) + '\n'


def read_components(comp_dir: Path, *, include_priv: bool) -> dict[str, list[str]]:
    """Every direct child of comp_dir with a CMakeLists.txt, mapped to the components it requires."""
    return {
        cml.parent.name: parse_requires(cml.read_text(encoding='utf-8'), include_priv=include_priv)
        for cml in sorted(comp_dir.glob('*/CMakeLists.txt'))
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        '--components-dir',
        type=Path,
        default=ROOT / 'components',
        help='directory holding the component subdirs (default: ./components)',
    )
    parser.add_argument(
        '-e', '--external', action='store_true', help='also include external components (IDF/managed) as nodes'
    )
    parser.add_argument('--no-priv', action='store_true', help='ignore PRIV_REQUIRES, use only public REQUIRES')
    parser.add_argument(
        '--direction',
        default='TD',
        choices=['TD', 'TB', 'LR', 'RL', 'BT'],
        help='Mermaid flowchart direction (default: TD)',
    )
    parser.add_argument('-o', '--output', type=Path, help='write to this file instead of stdout')
    args = parser.parse_args(argv)

    if not args.components_dir.is_dir():
        raise SystemExit(f'error: {args.components_dir} is not a directory')

    components = read_components(args.components_dir, include_priv=not args.no_priv)
    nodes, edges = build_graph(components, external=args.external)
    text = render_mermaid(nodes, edges, args.direction)

    if args.output:
        args.output.write_text(text, encoding='utf-8')
        print(f'wrote {args.output} ({len(nodes)} nodes, {len(edges)} edges)', file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
