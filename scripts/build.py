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
import shutil
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


def resolve_build_dir(board: str) -> Path:
    """Where to build. ``build/<board>`` locally, overridable with OPENSHOCK_BUILD_DIR.

    The build directory path ends up on the compiler command line (``-I<dir>/config``
    and the ``@<dir>/toolchain/cflags`` response file), so two boards built in
    differently-named directories produce different command lines and share nothing in
    ccache - even when their sdkconfig, and therefore every object, is identical.

    Locally the per-board directory is what you want, so several boards can coexist. Each
    CI job builds exactly one board, so it sets OPENSHOCK_BUILD_DIR to a fixed path and
    every board in a cache group then compiles byte-identical objects.
    """
    override = os.environ.get('OPENSHOCK_BUILD_DIR')
    if override:
        path = Path(override)
        return path if path.is_absolute() else ROOT / path
    return ROOT / 'build' / board


def write_sdkconfig_fragment(fragment: Path, build_dir: Path) -> Path:
    """Strip the board's GPIO assignments out of the fragment before idf.py sees it.

    boards/<board>.defaults holds two kinds of line: CONFIG_* settings, which are real
    Kconfig and belong in sdkconfig, and bare OPENSHOCK_* GPIO assignments, which are
    not. The latter are read by scripts/gen_env_header.py into openshock_board.h.

    Keeping the pins out of Kconfig is what lets boards sharing a chip and flash size
    produce byte-identical ESP-IDF objects: as Kconfig they landed in sdkconfig.h, which
    most of the framework includes, so a different LED pin invalidated everything.

    Kconfig would reject the bare lines outright, so the filtered copy is what idf.py is
    pointed at.
    """
    out = build_dir / 'board.defaults'
    kept = [
        line
        for line in fragment.read_text(encoding='utf-8').splitlines()
        if not re.match(r'\s*OPENSHOCK_[A-Z0-9_]+\s*=', line)
    ]
    body = (
        f'# Generated from {fragment.as_posix()} by scripts/build.py - do not edit.\n'
        '# The board GPIO assignments are stripped here; they are not Kconfig.\n'
        + '\n'.join(kept)
        + '\n'
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    # Write only on change, so touching it does not force a CMake reconfigure.
    if not out.is_file() or out.read_text(encoding='utf-8') != body:
        out.write_text(body, encoding='utf-8')
    return out


def main(argv: list[str]) -> int:
    if not argv:
        raise SystemExit(__doc__)

    board = argv[0]
    targets = argv[1:] or ['build']

    fragment = board_fragment(board)
    target = resolve_target(fragment)
    build_dir = resolve_build_dir(board)
    frag_rel = write_sdkconfig_fragment(fragment, build_dir).relative_to(ROOT).as_posix()

    idf_args = [
        '-C', str(ROOT),
        '-B', str(build_dir),
        f'-DIDF_TARGET={target}',
        f'-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;{frag_rel}',
        f'-DSDKCONFIG={build_dir / "sdkconfig"}',
        *targets,
    ]

    # Prefer an executable `idf.py` wrapper on PATH. install-esp-idf-action (and
    # an activated ESP-IDF shell) provide one that sets up the tool + venv
    # environment itself - the EIM install is not visible to a plain
    # `python tools/idf.py`. Restricted to POSIX: on Windows a bare idf.py isn't
    # directly executable, so fall through to the interpreter form there.
    idf_wrapper = shutil.which('idf.py') if os.name == 'posix' else None
    if idf_wrapper:
        cmd = [idf_wrapper, *idf_args]
    else:
        # Fall back to running tools/idf.py with the current interpreter, which
        # requires the ESP-IDF venv to already be active (IDF_PATH set).
        idf_path = os.environ.get('IDF_PATH')
        if not idf_path:
            raise SystemExit('error: IDF_PATH is not set - run the ESP-IDF export script first')
        cmd = [sys.executable, str(Path(idf_path) / 'tools' / 'idf.py'), *idf_args]

    env = dict(os.environ, OPENSHOCK_BOARD=board)
    print(f'+ OPENSHOCK_BOARD={board} {" ".join(cmd)}', flush=True)
    return subprocess.call(cmd, env=env)


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
