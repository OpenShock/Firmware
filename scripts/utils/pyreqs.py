"""Verify that this interpreter has every pin of a requirements file installed.

The project's build scripts (gen_staticfs.py, stamp_board.py, ...) run under whatever interpreter
CMake was handed, and they need requirements.txt. The ESP-IDF venv is not that interpreter: its
constraints pin click and cryptography below what requirements.txt wants, so the file must go into a
venv of the developer's own (docs/DEVELOPING.md, Python dependencies). This module only checks; it
never installs anything.

    python scripts/utils/pyreqs.py --check requirements.txt   # exit 1 naming the pins this interpreter lacks

Pins are `name==version` lines, optionally followed by an environment marker (`; sys_platform ==
'win32'`); pins whose marker does not hold here are skipped. The marker grammar is `packaging`'s,
taken from pip's vendored copy when `packaging` itself is not installed, so the check works in a
bare venv.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REQUIREMENTS = ROOT / 'requirements.txt'

# `name==version [; marker] [\]`; hash continuation lines are indented and do not match.
_PIN = re.compile(r'^([A-Za-z0-9][A-Za-z0-9._-]*)==([^\s\\;]+)\s*(?:;\s*([^\\]*?))?\s*\\?\s*$', re.MULTILINE)


@dataclass(frozen=True)
class Pin:
    name: str
    version: str
    marker: str | None = None

    def __str__(self) -> str:
        return f'{self.name}=={self.version}'


def parse_pins(text: str) -> list[Pin]:
    return [Pin(m.group(1).lower(), m.group(2), (m.group(3) or '').strip() or None) for m in _PIN.finditer(text)]


def marker_holds(marker: str) -> bool:
    try:
        from packaging.markers import Marker
    except ImportError:
        from pip._vendor.packaging.markers import Marker  # type: ignore[no-redef,assignment]
    return bool(Marker(marker).evaluate())


def installed_version(name: str) -> str | None:
    import importlib.metadata as metadata

    try:
        return metadata.version(name)
    except metadata.PackageNotFoundError:
        return None


def missing_pins(pins: list[Pin]) -> list[Pin]:
    """The pins that apply to this interpreter and are not installed at exactly their version."""
    return [
        pin
        for pin in pins
        if (pin.marker is None or marker_holds(pin.marker)) and installed_version(pin.name) != pin.version
    ]


def check(path: Path = REQUIREMENTS) -> list[Pin]:
    return missing_pins(parse_pins(path.read_text(encoding='utf-8')))


def report(missing: list[Pin], path: Path, python: str = sys.executable) -> str:
    names = ', '.join(str(p) for p in missing)
    return (
        f'error: {python} lacks {len(missing)} of the pins in {path.name}: {names}\n'
        f'The build scripts need an interpreter with {path.name} installed; see docs/DEVELOPING.md '
        f'(Python dependencies). Not the ESP-IDF venv: its constraints conflict with {path.name}.'
    )


def main(argv: list[str] | None = None) -> int:
    import argparse

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', metavar='REQUIREMENTS', type=Path, required=True)
    args = ap.parse_args(argv)
    missing = check(args.check)
    if missing:
        print(report(missing, args.check), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
