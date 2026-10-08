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
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# idf_component_register() argument keywords. A token equal to one of these ends
# the current value list and (for REQUIRES/PRIV_REQUIRES) starts a new one.
CMAKE_KEYWORDS = {
    'SRCS', 'SRC_DIRS', 'EXCLUDE_SRCS',
    'INCLUDE_DIRS', 'PRIV_INCLUDE_DIRS',
    'REQUIRES', 'PRIV_REQUIRES', 'REQUIRED_IDF_TARGETS',
    'EMBED_FILES', 'EMBED_TXTFILES', 'LDFRAGMENTS',
    'KCONFIG', 'KCONFIG_PROJBUILD', 'WHOLE_ARCHIVE',
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
                return text[start + 1:i]
    return text[start + 1:]  # unbalanced; take the rest


def parse_requires(cmake_text: str, include_priv: bool) -> list[str]:
    """Pull the REQUIRES (+ optionally PRIV_REQUIRES) token lists."""
    block = extract_register_block(strip_comments(cmake_text))
    tokens = block.split()

    deps: list[str] = []
    active = None  # None | 'collect'
    for tok in tokens:
        if tok in CMAKE_KEYWORDS:
            if tok == 'REQUIRES' or (tok == 'PRIV_REQUIRES' and include_priv):
                active = 'collect'
            else:
                active = None
            continue
        if active == 'collect':
            deps.append(tok)

    # de-dupe while preserving order
    seen = set()
    ordered = []
    for d in deps:
        if d not in seen:
            seen.add(d)
            ordered.append(d)
    return ordered


def topo_order(nodes: set[str], edges: set[tuple[str, str]]) -> list[str]:
    """Dependencies first. Edge (dep, dependent). Alphabetical tie-break; any
    nodes left in a cycle are appended in sorted order so output stays total."""
    indeg = {n: 0 for n in nodes}
    adj = {n: [] for n in nodes}
    for dep, dependent in edges:
        adj[dep].append(dependent)
        indeg[dependent] += 1

    ready = sorted(n for n in nodes if indeg[n] == 0)
    order = []
    while ready:
        n = ready.pop(0)
        order.append(n)
        for m in sorted(adj[n]):
            indeg[m] -= 1
            if indeg[m] == 0:
                ready.append(m)
        ready.sort()

    if len(order) < len(nodes):
        order.extend(sorted(nodes - set(order)))
    return order


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--components-dir', default=str(ROOT / 'components'),
                        help='directory holding the component subdirs (default: ./components)')
    parser.add_argument('-e', '--external', action='store_true',
                        help='also include external components (IDF/managed) as nodes')
    parser.add_argument('--no-priv', action='store_true',
                        help='ignore PRIV_REQUIRES, use only public REQUIRES')
    parser.add_argument('--direction', default='TD', choices=['TD', 'TB', 'LR', 'RL', 'BT'],
                        help='Mermaid flowchart direction (default: TD)')
    parser.add_argument('-o', '--output', help='write to this file instead of stdout')
    args = parser.parse_args(argv)

    comp_dir = Path(args.components_dir)
    if not comp_dir.is_dir():
        raise SystemExit(f'error: {comp_dir} is not a directory')

    # Local components: direct children of components/ with a CMakeLists.txt.
    local = {}
    for cml in sorted(comp_dir.glob('*/CMakeLists.txt')):
        name = cml.parent.name
        local[name] = parse_requires(cml.read_text(encoding='utf-8'),
                                     include_priv=not args.no_priv)

    edges: set[tuple[str, str]] = set()
    nodes: set[str] = set(local)
    for name, deps in local.items():
        for dep in deps:
            is_local = dep in local
            if not is_local and not args.external:
                continue
            edges.add((dep, name))  # dependency -> dependent
            nodes.add(dep)

    order = topo_order(nodes, edges)
    ids = {name: col_id(i) for i, name in enumerate(order)}

    lines = [f'flowchart {args.direction}']
    for name in order:
        lines.append(f'    {ids[name]}({label_for(name)})')
    # Edges grouped by source in node order for stable, readable output.
    for src in order:
        for dst in order:
            if (src, dst) in edges:
                lines.append(f'    {ids[src]} --> {ids[dst]}')

    text = '\n'.join(lines) + '\n'
    if args.output:
        Path(args.output).write_text(text, encoding='utf-8')
        print(f'wrote {args.output} ({len(nodes)} nodes, {len(edges)} edges)', file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
