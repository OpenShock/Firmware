#!/usr/bin/env python3
"""Native ESP-IDF build driver.

Usage:
    python scripts/build.py <board> [idf.py targets/args ...]

Resolves the IDF target from boards/<board>.defaults, sets OPENSHOCK_BOARD (so the
root CMakeLists generates the env header), layers sdkconfig.defaults + the board
fragment, and runs idf.py into build/<board>/. Replaces the PlatformIO per-env
build so CI and local builds share one entry point.

Examples:
    python scripts/build.py OpenShock-Core-V2                 # full build
    python scripts/build.py OpenShock-Core-V2 app bootloader partition-table
"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def board_fragment(board: str) -> Path:
    fragment = ROOT / 'boards' / f'{board}.defaults'
    if not fragment.is_file():
        avail = ', '.join(sorted(p.stem for p in (ROOT / 'boards').glob('*.defaults')))
        raise SystemExit(f'error: unknown board {board!r}\n  available: {avail}')
    return fragment


def resolve_target(fragment: Path) -> str:
    for line in fragment.read_text(encoding='utf-8').splitlines():
        m = re.match(r'\s*CONFIG_IDF_TARGET\s*=\s*"([^"]+)"', line)
        if m:
            return m.group(1)
    raise SystemExit(f'error: {fragment} does not set CONFIG_IDF_TARGET')


def main(argv: list[str]) -> int:
    if not argv:
        raise SystemExit(__doc__)

    board = argv[0]
    targets = argv[1:] or ['build']

    fragment = board_fragment(board)
    target = resolve_target(fragment)
    build_dir = ROOT / 'build' / board
    frag_rel = fragment.relative_to(ROOT).as_posix()

    # Invoke idf.py through the current interpreter so this works on Windows too
    # (where a bare "idf.py" isn't directly executable). Requires the ESP-IDF
    # environment to be exported (IDF_PATH set, venv active).
    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        raise SystemExit('error: IDF_PATH is not set - run the ESP-IDF export script first')
    idf_py = Path(idf_path) / 'tools' / 'idf.py'

    cmd = [
        sys.executable, str(idf_py),
        '-C', str(ROOT),
        '-B', str(build_dir),
        f'-DIDF_TARGET={target}',
        f'-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;{frag_rel}',
        f'-DSDKCONFIG={build_dir / "sdkconfig"}',
        *targets,
    ]

    env = dict(os.environ, OPENSHOCK_BOARD=board)
    print(f'+ OPENSHOCK_BOARD={board} {" ".join(cmd)}', flush=True)
    return subprocess.call(cmd, env=env)


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
