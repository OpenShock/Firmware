#!/usr/bin/env python3
"""Generate the OpenShock build headers.

Native ESP-IDF replacement for the PlatformIO ``embed_env_vars.py`` extra_script.
Two headers are written, split by how often their contents change:

``openshock_board.h``
    Board and chip identity, build mode, log level, and the board's GPIO
    assignments. Changes when the board changes, not when the tree does.

``openshock_version.h``
    Firmware version and git commit. Changes on *every* commit.

The split exists for ccache. Both headers used to be one file that the build
force-included into every translation unit, so a new commit changed the
preprocessed text of every source in the project - all of ESP-IDF included - and
nothing could ever be reused. Neither header is force-included now:
``openshock_board.h`` is pulled in by ``OpenShock.h`` (so only OpenShock's own
components see it), and ``openshock_version.h`` by the handful of translation
units that actually report a version.

The GPIO assignments live here rather than in Kconfig for the same reason. As
Kconfig symbols they landed in ``sdkconfig.h``, which most of ESP-IDF includes,
so two boards that differed only by an LED pin shared no compiled objects at
all. Keeping them out of ``sdkconfig.h`` lets every board with the same chip and
flash size compile byte-identical framework objects.

Values come from (highest priority first):
  1. ``OPENSHOCK_*`` process environment variables (CI provides the version etc.)
  2. the board fragment (``boards/<board>.defaults``), for the GPIO assignments
  3. computed defaults (local dev version fallback, board/chip macros)
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from utils import shorthands  # noqa: E402

# Keys whose value must always be emitted as a C string, even when it happens to
# be all-numeric (a bare numeric macro breaks string concatenation / #if usage).
FORCE_STRING_KEYS = {
    'OPENSHOCK_FW_GIT_COMMIT',
    'OPENSHOCK_FW_VERSION_BUILD',
    # Semver allows purely-numeric prerelease identifiers (e.g. 1.2.3-1); keep it
    # a string so it isn't emitted as a bare int macro.
    'OPENSHOCK_FW_VERSION_PRERELEASE',
}

# The board's GPIO assignments, with the defaults a board fragment may override.
# `int` entries are emitted verbatim; `bool` entries as 0/1, since RgbLedDriver.cpp
# tests one with `#if`. Every key is always emitted: CompatibilityChecks.cpp puts the
# pins through static_assert, so an undefined macro is a compile error rather than a
# silently disabled feature.
BOARD_PINS: dict[str, tuple[str, int]] = {
    'OPENSHOCK_RF_TX_GPIO': ('int', -1),
    'OPENSHOCK_ESTOP_PIN': ('int', -1),
    'OPENSHOCK_LED_GPIO': ('int', -1),
    'OPENSHOCK_LED_WS2812B': ('int', -1),
    'OPENSHOCK_LED_SWAP_RG_CHANNELS': ('bool', 0),
}


def macroify(s: str) -> str:
    return s.upper().replace('-', '').replace('_', '')


def split_semver(version: str):
    pattern = (
        r'^v?(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)'
        r'(?:-(?P<prerelease>[0-9A-Za-z.-]+))?(?:\+(?P<build>[0-9A-Za-z.-]+))?$'
    )
    m = re.match(pattern, version)
    if not m:
        raise ValueError(f'Invalid semver version: {version!r}')
    return (
        int(m.group('major')),
        int(m.group('minor')),
        int(m.group('patch')),
        m.group('prerelease'),
        m.group('build'),
    )


def c_string(value: str) -> str:
    escaped = value.replace('\\', '\\\\').replace('"', '\\"')
    return f'"{escaped}"'


def serialize(key: str, value) -> str:
    """Render a macro value: raw int when numeric, else a C string."""
    if key in FORCE_STRING_KEYS:
        return c_string(str(value))
    s = str(value)
    try:
        return str(int(s))
    except ValueError:
        return c_string(s)


def git_commit() -> str | None:
    commit = os.environ.get('OPENSHOCK_FW_GIT_COMMIT')
    if commit:
        return commit
    try:
        return subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except Exception:
        return None


def resolve_version() -> str:
    version = os.environ.get('OPENSHOCK_FW_VERSION')
    if version:
        return version
    if shorthands.is_github_ci():
        raise SystemExit('error: OPENSHOCK_FW_VERSION must be set for CI builds')
    # Local dev fallback - a valid semver so split_semver() is happy.
    commit = git_commit()
    return '0.0.0-local' + (f'+{commit[:7]}' if commit else '')


def read_board_pins(fragment: Path) -> dict[str, int]:
    """Read the bare ``OPENSHOCK_*=`` assignments out of a board fragment.

    These sit alongside the ``CONFIG_*`` lines but are deliberately not Kconfig, so
    scripts/build.py strips them before handing the fragment to idf.py. Anything
    unrecognised is an error rather than a silent no-op - a typo in a pin name would
    otherwise leave the board on the default and be very hard to spot.
    """
    values = {key: default for key, (_, default) in BOARD_PINS.items()}
    if not fragment.is_file():
        raise SystemExit(f'error: board fragment {fragment} not found')

    for lineno, raw in enumerate(fragment.read_text(encoding='utf-8').splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#') or line.startswith('CONFIG_'):
            continue
        if '=' not in line:
            raise SystemExit(f'error: {fragment}:{lineno}: expected KEY=VALUE, got {line!r}')
        key, _, value = line.partition('=')
        key, value = key.strip(), value.strip()
        if key not in BOARD_PINS:
            known = ', '.join(sorted(BOARD_PINS))
            raise SystemExit(f'error: {fragment}:{lineno}: unknown board setting {key!r}\n  known: {known}')

        kind, _ = BOARD_PINS[key]
        if kind == 'bool':
            if value not in ('y', 'n'):
                raise SystemExit(f'error: {fragment}:{lineno}: {key} is a boolean, expected y or n, got {value!r}')
            values[key] = 1 if value == 'y' else 0
        else:
            try:
                values[key] = int(value)
            except ValueError:
                raise SystemExit(f'error: {fragment}:{lineno}: {key} expects an integer GPIO, got {value!r}')

    return values


def render(title: str, defines: dict[str, object]) -> str:
    lines = [
        '// Generated by scripts/gen_env_header.py - do not edit.',
        f'// {title}',
        '#pragma once',
        '',
    ]
    for key in sorted(defines):
        lines.append(f'#define {key} {serialize(key, defines[key])}')
    lines.append('')
    return '\n'.join(lines)


def write_if_changed(path: Path, content: str) -> bool:
    """Write only when the bytes differ.

    CMake re-runs this generator on every configure. Rewriting an identical header
    would still move its mtime, which pointlessly disturbs ninja and ccache on
    incremental builds. Returns True when the file was actually written.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text(encoding='utf-8') == content:
        return False
    path.write_text(content, encoding='utf-8')
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out-dir', required=True, help='directory to write the headers into')
    ap.add_argument('--board', required=True, help='board name, e.g. OpenShock-Core-V2')
    ap.add_argument('--chip', required=True, help='chip, e.g. ESP32-S3')
    ap.add_argument('--fragment', required=True, help='path to boards/<board>.defaults')
    ap.add_argument(
        '--emit',
        choices=('board', 'version', 'both'),
        default='both',
        help=(
            'which headers to write. The board header is generated once at CMake configure '
            'time; the version header is regenerated on every build, so that a new commit '
            'is picked up without a reconfigure (see the root CMakeLists.txt).'
        ),
    )
    args = ap.parse_args()

    # Build mode: release for CI builds of master/beta/tags, debug otherwise.
    is_ci = shorthands.is_github_ci()
    is_release = is_ci and (
        shorthands.get_github_ref_name() in ('master', 'beta')
        or shorthands.is_github_pr_into('master')
        or shorthands.is_github_pr_into('beta')
        or shorthands.is_github_tag()
    )
    is_beta = is_ci and (
        shorthands.get_github_ref_name() == 'beta' or shorthands.is_github_pr_into('beta')
    )
    mode = 'release' if is_release else 'debug'

    # --- openshock_board.h ---------------------------------------------------
    board: dict[str, object] = {}

    board['OPENSHOCK_FW_BOARD'] = args.board
    board['OPENSHOCK_FW_BOARD_' + macroify(args.board)] = 1
    board['OPENSHOCK_FW_CHIP'] = args.chip.upper()
    board['OPENSHOCK_FW_CHIP_' + macroify(args.chip)] = 1
    board['OPENSHOCK_FW_MODE'] = mode

    # Log level by build mode (formerly .env.development / .env.production):
    #   debug -> VERBOSE(5), release -> INFO(3), beta -> DEBUG(4).
    if is_beta:
        log_level = 4
    elif is_release:
        log_level = 3
    else:
        log_level = 5
    board['OPENSHOCK_LOG_LEVEL'] = log_level

    board.update(read_board_pins(Path(args.fragment)))

    # --- openshock_version.h -------------------------------------------------
    version_defines: dict[str, object] = {}

    version = resolve_version()
    major, minor, patch, prerelease, build = split_semver(version)
    version_defines['OPENSHOCK_FW_VERSION'] = version
    version_defines['OPENSHOCK_FW_VERSION_MAJOR'] = major
    version_defines['OPENSHOCK_FW_VERSION_MINOR'] = minor
    version_defines['OPENSHOCK_FW_VERSION_PATCH'] = patch
    if prerelease is not None:
        version_defines['OPENSHOCK_FW_VERSION_PRERELEASE'] = prerelease
    if build is not None:
        version_defines['OPENSHOCK_FW_VERSION_BUILD'] = build

    commit = git_commit()
    if commit is not None:
        version_defines['OPENSHOCK_FW_GIT_COMMIT'] = commit

    out_dir = Path(args.out_dir)
    wanted = {'board': ('board',), 'version': ('version',), 'both': ('board', 'version')}[args.emit]
    written = []
    for which, name, title, defines in (
        ('board', 'openshock_board.h', 'Board identity, build mode and GPIO assignments.', board),
        ('version', 'openshock_version.h', 'Firmware version and git commit. Changes every commit.', version_defines),
    ):
        if which not in wanted:
            continue
        if write_if_changed(out_dir / name, render(title, defines)):
            written.append(name)

    if written:
        print(f'gen_env_header: wrote {", ".join(written)} to {out_dir}')
    else:
        print(f'gen_env_header: {out_dir} already up to date')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
