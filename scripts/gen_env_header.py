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

The GPIO assignments (parsed by scripts/utils/boards.py) live here rather than in Kconfig for the same reason. As
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
from utils import boards, shorthands  # noqa: E402

# Keys whose value must always be emitted as a C string, even when it happens to
# be all-numeric (a bare numeric macro breaks string concatenation / #if usage).
FORCE_STRING_KEYS = {
    'OPENSHOCK_FW_GIT_COMMIT',
    'OPENSHOCK_FW_VERSION_BUILD',
    # Semver allows purely-numeric prerelease identifiers (e.g. 1.2.3-1); keep it
    # a string so it isn't emitted as a bare int macro.
    'OPENSHOCK_FW_VERSION_PRERELEASE',
}

# Keys emitted verbatim (not through serialize()): C integer literals with a suffix.
RAW_KEYS = {'OPENSHOCK_GPIO_BYPASS_MASK'}


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
    if key in RAW_KEYS:
        return str(value)
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
        return subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True, stderr=subprocess.DEVNULL).strip()
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


def board_defines(spec: boards.BoardSpec) -> dict[str, object]:
    """The board's hardware description as macros.

    Every key is always emitted, with -1 / 0 for what the board lacks, so a consumer can
    rely on the macro existing. These are the values compiled into the firmware's board
    descriptor (BoardDescriptor.cpp); scripts/stamp_board.py writes the same fields into
    the app image after the build, which is what a per-chip build relies on.
    """
    defines: dict[str, object] = dict(spec.pins)
    defines['OPENSHOCK_LED_SWAP_RG_CHANNELS'] = 1 if spec.led_swap_rg else 0
    # The chip-unsafe pins this board is allowed to use anyway, as a bit mask (Chipset.h).
    defines['OPENSHOCK_GPIO_BYPASS_MASK'] = f'0x{spec.gpio_bypass_mask:X}ULL'
    return defines


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
    # A release-candidate tag (e.g. 1.5.0-rc.7) is a beta-channel build too, so it gets beta's log level.
    ref_name = shorthands.get_github_ref_name() or ''
    tag_prerelease = ref_name.split('-', 1)[1].lower() if shorthands.is_github_tag() and '-' in ref_name else ''
    is_beta = is_ci and (
        ref_name == 'beta' or shorthands.is_github_pr_into('beta') or tag_prerelease.startswith(('rc', 'beta'))
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

    board.update(board_defines(boards.read_board_fragment(Path(args.fragment), args.board)))

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
