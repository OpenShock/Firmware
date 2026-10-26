#!/usr/bin/env python3
"""Native ESP-IDF build driver.

Usage:
    python scripts/build.py <board> [idf.py targets/args ...]
    python scripts/build.py --variant <variant-id> [idf.py targets/args ...]

Board mode builds boards/<board>.defaults into build/<board>/ and stamps the board into the image.
Variant mode is what CI builds: one unstamped compile for every board of a build variant
(scripts/utils/boards.py) into build/<variant-id>/; scripts/package_boards.py stamps each board.
An unknown variant id lists the variants and their boards.

CI builds of the stable channel compile with -Os, everything else with -Og (utils/github.py:build_optimization).
OPENSHOCK_FW_OPTIMIZATION=size|debug overrides it; a local size build goes to build/<board>-size/.

Examples:
    python scripts/build.py OpenShock-Core-V2                 # full build
    python scripts/build.py OpenShock-Core-V2 app bootloader partition-table
    python scripts/build.py --variant esp32s3-6832dbd5 app bootloader partition-table
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

from utils import github, idfenv, partitions, pyreqs
from utils.boards import board_fragment, find_variant, flash_size, kconfig_lines, read_board_fragment
from utils.files import write_if_changed
from utils.paths import ROOT

# The Kconfig each level selects (OPENSHOCK_FW_OPTIMIZATION, else github.build_optimization()). Size also drops the
# assert text (file, line, expression), about 54 KB: a failed assert still aborts with a backtrace, which the
# release's ELF decodes.
OPTIMIZATION_KCONFIG = {
    'debug': ('CONFIG_COMPILER_OPTIMIZATION_DEBUG=y', 'CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_ENABLE=y'),
    'size': ('CONFIG_COMPILER_OPTIMIZATION_SIZE=y', 'CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y'),
}


def resolve_optimization() -> str:
    level = os.environ.get('OPENSHOCK_FW_OPTIMIZATION') or github.build_optimization()
    if level not in OPTIMIZATION_KCONFIG:
        raise SystemExit(
            f'error: OPENSHOCK_FW_OPTIMIZATION must be one of {", ".join(OPTIMIZATION_KCONFIG)}, not {level!r}'
        )
    return level


class BuildInputs(NamedTuple):
    """What one idf.py configure needs: the target, the board to stamp, and the sdkconfig fragment to layer."""

    target: str
    board: str | None  # None for a variant: it is not stamped
    build_dir: Path
    fragment: Path
    fragment_body: str


def resolve_build_dir(name: str, optimization: str = 'debug') -> Path:
    """``build/<name>``, or OPENSHOCK_BUILD_DIR. CI fixes the path: it is on the compiler command line, so it is in ccache's hash.

    A non-debug build gets ``build/<name>-<level>``: an existing sdkconfig wins over SDKCONFIG_DEFAULTS, so switching the
    level inside one build directory would silently keep the old one.
    """
    override = os.environ.get('OPENSHOCK_BUILD_DIR')
    if override:
        path = Path(override)
        return path if path.is_absolute() else ROOT / path
    return ROOT / 'build' / (name if optimization == 'debug' else f'{name}-{optimization}')


def optimization_kconfig(optimization: str) -> str:
    # Last in the fragment, after the board's lines. Not part of the variant id: every variant compiles at either level.
    return f'# Optimization: {optimization}\n' + ''.join(f'{line}\n' for line in OPTIMIZATION_KCONFIG[optimization])


def partition_table_kconfig(size: str) -> str:
    # Relative to the project, which is how ESP-IDF resolves CONFIG_PARTITION_TABLE_CUSTOM_FILENAME
    csv = partitions.table_csv_for_flash_size(size).relative_to(partitions.ROOT).as_posix()
    return (
        f'# Partition table for a {size} flash\n'
        'CONFIG_PARTITION_TABLE_CUSTOM=y\n'
        f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{csv}"\n'
    )


def variant_inputs(variant_id: str, optimization: str = 'debug') -> BuildInputs:
    variant = find_variant(variant_id)
    build_dir = resolve_build_dir(variant.id, optimization)
    body = (
        f'# Build variant {variant.id} ({", ".join(variant.boards)})\n'
        + variant.sdkconfig_fragment()
        + optimization_kconfig(optimization)
    )
    return BuildInputs(variant.target, None, build_dir, build_dir / 'variant.defaults', body)


def board_inputs(board: str, optimization: str = 'debug') -> BuildInputs:
    fragment = board_fragment(board)
    target = read_board_fragment(fragment).target  # parsed whole, so a typo fails now rather than at the stamp
    build_dir = resolve_build_dir(board, optimization)
    # Only the Kconfig lines: the OPENSHOCK_* pins are stamped, and Kconfig would reject them. The partition table
    # follows from the flash size (sdkconfig.defaults names the 4 MB one; an 8 MB board gets its own here).
    body = (
        f'# Board {board}, without its pins\n'
        + '\n'.join(kconfig_lines(fragment))
        + '\n'
        + partition_table_kconfig(flash_size(board))
        + optimization_kconfig(optimization)
    )
    return BuildInputs(target, board, build_dir, build_dir / 'board.defaults', body)


def run_idf(idf_args: list[str]) -> int:
    # An idf.py wrapper on PATH (install-esp-idf-action, an activated ESP-IDF shell) sets up its own
    # environment. On Windows a bare idf.py is not executable, so run tools/idf.py with an interpreter:
    # the ESP-IDF venv's when known (an installer-registered install activates here if the shell did not).
    idf_wrapper = shutil.which('idf.py') if os.name == 'posix' else None
    # $IDF_PATH/tools on PATH (a restored CI install) is no wrapper: its `env python` shebang takes
    # whichever interpreter leads PATH, which need not be the ESP-IDF venv.
    idf_path = os.environ.get('IDF_PATH')
    if idf_wrapper and idf_path and Path(idf_wrapper).resolve() == (Path(idf_path) / 'tools' / 'idf.py').resolve():
        idf_wrapper = None
    if idf_wrapper:
        cmd = [idf_wrapper, *idf_args]
    else:
        python = idfenv.activate()
        idf_path = os.environ.get('IDF_PATH')
        if not python or not idf_path:
            raise SystemExit(
                'error: IDF_PATH is not set and no ESP-IDF install was found - run the ESP-IDF export script first'
            )
        cmd = [python, str(Path(idf_path) / 'tools' / 'idf.py'), *idf_args]

    # idf.py prints non-ASCII progress glyphs; without UTF-8 mode, Python on Windows encodes stdout
    # with the legacy console code page and crashes on them (UnicodeEncodeError).
    env = {**os.environ}
    env.setdefault('PYTHONUTF8', '1')

    print(f'+ {" ".join(cmd)}', flush=True)
    return subprocess.call(cmd, env=env)


def publish_compile_commands(build_dir: Path) -> None:
    """Copy the build's compile database to build/compile_commands.json, where .clangd looks for it.

    One shared location, so clangd follows whichever board or variant was built last.
    """
    src = build_dir / 'compile_commands.json'
    if not src.is_file():
        return
    dst = ROOT / 'build' / 'compile_commands.json'
    if dst.resolve() == src.resolve():
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--variant', help='build this variant (one unstamped compile) instead of a board')
    ap.add_argument('args', nargs=argparse.REMAINDER, help='<board> (unless --variant), then idf.py targets/args')
    args = ap.parse_args(argv)

    optimization = resolve_optimization()
    if args.variant:
        targets = args.args or ['build']
        inputs = variant_inputs(args.variant, optimization)
    else:
        if not args.args:
            ap.error('a board or --variant is required')
        targets = args.args[1:] or ['build']
        inputs = board_inputs(args.args[0], optimization)
    frag, build_dir = inputs.fragment, inputs.build_dir
    # Generated, and written only on change so an unchanged fragment does not force a reconfigure.
    write_if_changed(frag, '# Generated by scripts/build.py - do not edit.\n' + inputs.fragment_body)
    # Relative to the project where it can be; ESP-IDF takes an absolute entry too, for a build dir outside the repo.
    frag_rel = (frag.relative_to(ROOT) if frag.is_relative_to(ROOT) else frag).as_posix()

    # The project's scripts run under this interpreter, which therefore needs requirements.txt. Check
    # before idf.py starts rather than minutes into the build; nothing is installed on the developer's
    # behalf, and never into the ESP-IDF venv.
    missing = pyreqs.check()
    if missing:
        raise SystemExit(pyreqs.report(missing, pyreqs.REQUIREMENTS))

    rc = run_idf(
        [
            '-C',
            str(ROOT),
            '-B',
            str(build_dir),
            f'-DOPENSHOCK_PYTHON={sys.executable}',
            f'-DIDF_TARGET={inputs.target}',
            f'-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;{frag_rel}',
            f'-DSDKCONFIG={build_dir / "sdkconfig"}',
            # Empty for a variant, which also clears a board an earlier configure cached.
            f'-DOPENSHOCK_BOARD={inputs.board or ""}',
            *targets,
        ]
    )
    if rc == 0:
        publish_compile_commands(build_dir)
    return rc


if __name__ == '__main__':
    raise SystemExit(main())
