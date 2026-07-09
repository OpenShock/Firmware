#!/usr/bin/env python3
"""Generate the OpenShock environment header, force-included into every TU.

Native ESP-IDF replacement for the PlatformIO ``embed_env_vars.py`` extra_script.
It emits the firmware version, API/CDN domains, board/chip identity, build mode
and log level as preprocessor ``#define``s. The header is force-included by the
build (see the root ``CMakeLists.txt``), so ``OpenShock.h`` and friends see the
same macros they used to get from PlatformIO ``-D`` flags.

Values come from (highest priority first):
  1. ``OPENSHOCK_*`` process environment variables (CI provides the version etc.)
  2. the ``.env`` / ``.env.<mode>`` files (API/CDN domains, hostname, log level)
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
    # Local dev fallback — a valid semver so split_semver() is happy.
    commit = git_commit()
    return '0.0.0-local' + (f'+{commit[:7]}' if commit else '')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', required=True, help='header file to write')
    ap.add_argument('--board', required=True, help='board name, e.g. OpenShock-Core-V2')
    ap.add_argument('--chip', required=True, help='chip, e.g. ESP32-S3')
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

    defines: dict[str, object] = {}

    # Version.
    version = resolve_version()
    major, minor, patch, prerelease, build = split_semver(version)
    defines['OPENSHOCK_FW_VERSION'] = version
    defines['OPENSHOCK_FW_VERSION_MAJOR'] = major
    defines['OPENSHOCK_FW_VERSION_MINOR'] = minor
    defines['OPENSHOCK_FW_VERSION_PATCH'] = patch
    if prerelease is not None:
        defines['OPENSHOCK_FW_VERSION_PRERELEASE'] = prerelease
    if build is not None:
        defines['OPENSHOCK_FW_VERSION_BUILD'] = build

    # Board / chip identity (+ macros used for conditional compilation).
    defines['OPENSHOCK_FW_BOARD'] = args.board
    defines['OPENSHOCK_FW_BOARD_' + macroify(args.board)] = 1
    defines['OPENSHOCK_FW_CHIP'] = args.chip.upper()
    defines['OPENSHOCK_FW_CHIP_' + macroify(args.chip)] = 1

    # Build mode + git commit.
    defines['OPENSHOCK_FW_MODE'] = mode
    commit = git_commit()
    if commit is not None:
        defines['OPENSHOCK_FW_GIT_COMMIT'] = commit

    # Log level by build mode (formerly .env.development / .env.production):
    #   debug -> VERBOSE(5), release -> INFO(3), beta -> DEBUG(4).
    if is_beta:
        log_level = 4
    elif is_release:
        log_level = 3
    else:
        log_level = 5
    defines['OPENSHOCK_LOG_LEVEL'] = log_level
    defines['CORE_DEBUG_LEVEL'] = log_level if (not is_release or is_beta) else 2

    lines = [
        '// Generated by scripts/gen_env_header.py — do not edit.',
        '#pragma once',
        '',
    ]
    for key in sorted(defines):
        lines.append(f'#define {key} {serialize(key, defines[key])}')
    lines.append('')

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\n'.join(lines), encoding='utf-8')
    print(f'gen_env_header: wrote {len(defines)} defines to {out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
