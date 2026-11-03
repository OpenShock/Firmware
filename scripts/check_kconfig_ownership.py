#!/usr/bin/env python3
"""Check that every component reads only the CONFIG_OPENSHOCK_* options it may depend on.

An option belongs to the component whose ``components/<name>/Kconfig`` defines it. A component may read
its own options and those of the components it requires: its REQUIRES and PRIV_REQUIRES, plus whatever
those export through their public REQUIRES. Every option exists in sdkconfig.h of every build, so the
compiler can't catch a component reading another's option behind its back; this does.

Settings more than one component reads go in the lowest component they all depend on (usually common).
Host tests (``host_test/``) are not checked: they compile sources from several components on purpose.

Usage:
    python scripts/check_kconfig_ownership.py
"""

import argparse
import re
import sys
from pathlib import Path

from gen_dep_graph import read_components

from utils.paths import ROOT

# Only config entries become CONFIG_* macros; a choice's own name never does
CONFIG_DEFINITION = re.compile(r'^\s*(?:menuconfig|config)\s+(OPENSHOCK_\w+)', re.MULTILINE)
CONFIG_USE = re.compile(r'\bCONFIG_(OPENSHOCK_\w+)')
SOURCE_SUFFIXES = {'.c', '.cpp', '.h', '.hpp'}

Violation = tuple[str, Path, str, str | None]  # (component, file, option, owner or None if undefined)


def read_owners(comp_dir: Path) -> dict[str, str]:
    """Option name (without CONFIG_) -> the component whose Kconfig defines it."""
    owners: dict[str, str] = {}
    for kconfig in sorted(comp_dir.glob('*/Kconfig')):
        for option in CONFIG_DEFINITION.findall(kconfig.read_text(encoding='utf-8')):
            owners[option] = kconfig.parent.name
    return owners


def allowed_owners(name: str, private: dict[str, list[str]], public: dict[str, list[str]]) -> set[str]:
    """The component itself, its direct requirements, and everything those export through public REQUIRES."""
    allowed = {name}
    pending = list(private.get(name, []))
    while pending:
        dep = pending.pop()
        if dep not in allowed:
            allowed.add(dep)
            pending.extend(public.get(dep, []))
    return allowed


def source_files(component: Path) -> list[Path]:
    return sorted(
        path
        for path in component.rglob('*')
        if path.suffix in SOURCE_SUFFIXES and 'host_test' not in path.relative_to(component).parts
    )


def find_violations(comp_dir: Path) -> list[Violation]:
    owners = read_owners(comp_dir)
    private = read_components(comp_dir, include_priv=True)
    public = read_components(comp_dir, include_priv=False)

    violations: list[Violation] = []
    for name in sorted(private):
        allowed = allowed_owners(name, private, public)
        for path in source_files(comp_dir / name):
            for option in sorted(set(CONFIG_USE.findall(path.read_text(encoding='utf-8', errors='replace')))):
                owner = owners.get(option)
                if owner not in allowed:
                    violations.append((name, path, option, owner))
    return violations


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        '--components-dir',
        type=Path,
        default=ROOT / 'components',
        help='directory holding the component subdirs (default: ./components)',
    )
    args = parser.parse_args(argv)

    violations = find_violations(args.components_dir)
    for name, path, option, owner in violations:
        where = path.relative_to(args.components_dir.parent).as_posix()
        if owner is None:
            print(f'{where}: CONFIG_{option} is not defined in any components/*/Kconfig', file=sys.stderr)
        else:
            print(f'{where}: CONFIG_{option} belongs to {owner}, which {name} does not require', file=sys.stderr)
    if violations:
        print(
            f'{len(violations)} Kconfig ownership violation(s): move the option to a component both depend on',
            file=sys.stderr,
        )
        return 1
    print('Kconfig ownership: every CONFIG_OPENSHOCK_* use is in a component that requires its owner')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
