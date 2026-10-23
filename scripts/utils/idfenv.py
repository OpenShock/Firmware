"""Find and activate an ESP-IDF install when the shell has not done it.

The Espressif installer (EIM) records every install in ``eim_idf.json`` under the tools directory
with an activation script per install. Run with ``-e`` the script prints the environment it would
set as ``KEY=VALUE`` lines instead of applying it: ``PATH`` holds only the directories to prepend,
``SYSTEM_PATH`` the caller's own PATH. This module applies that dump to ``os.environ``, which lets
scripts/build.py (and so the VS Code tasks) run from a plain terminal.

A shell that already ran the export script wins: nothing is touched when IDF_PATH is set.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from collections.abc import MutableMapping
from pathlib import Path


def _candidate_metadata() -> list[Path]:
    paths = []
    tools = os.environ.get('IDF_TOOLS_PATH')
    if tools:
        paths.append(Path(tools) / 'eim_idf.json')
    if os.name == 'nt':
        paths.append(Path(r'C:\Espressif\tools') / 'eim_idf.json')
    home = Path.home()
    paths += [home / '.espressif' / 'tools' / 'eim_idf.json', home / '.espressif' / 'eim_idf.json']
    return paths


def find_install() -> dict | None:
    """The selected install from the first eim_idf.json found, or None."""
    for meta in _candidate_metadata():
        if not meta.is_file():
            continue
        data = json.loads(meta.read_text(encoding='utf-8'))
        installs = data.get('idfInstalled') or []
        selected = data.get('idfSelectedId')
        for install in installs:
            if install.get('id') == selected:
                return install
        if installs:
            return installs[0]
    return None


def env_dump(activation_script: str) -> str:
    script = Path(activation_script)
    if script.suffix.lower() == '.ps1':
        shell = 'powershell' if os.name == 'nt' else 'pwsh'
        cmd = [shell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script), '-e']
    else:
        cmd = ['sh', str(script), '-e']
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def apply_env_dump(dump: str, environ: MutableMapping[str, str]) -> None:
    """Apply KEY=VALUE lines to environ: PATH is prepended, SYSTEM_PATH ignored, the rest set."""
    for line in dump.splitlines():
        key, sep, value = line.partition('=')
        if not sep or not key.strip():
            continue
        key = key.strip()
        if key == 'SYSTEM_PATH':
            continue
        if key == 'PATH':
            new = [d for d in value.split(os.pathsep) if d]
            environ['PATH'] = os.pathsep.join([*new, environ.get('PATH', '')])
        else:
            environ[key] = value


def venv_python(environ: MutableMapping[str, str], install: dict | None = None) -> str | None:
    if install and install.get('python') and Path(install['python']).is_file():
        return install['python']
    venv = environ.get('IDF_PYTHON_ENV_PATH')
    if venv:
        exe = Path(venv) / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
        if exe.is_file():
            return str(exe)
    return None


def activate(environ: MutableMapping[str, str] | None = None) -> str | None:
    """Ensure ESP-IDF is on the environment; returns the interpreter to run idf.py with.

    Returns None when no install can be found (the caller decides whether that is fatal).
    """
    if environ is None:
        environ = os.environ
    if environ.get('IDF_PATH'):
        return venv_python(environ) or sys.executable
    install = find_install()
    if not install or not install.get('activationScript'):
        return None
    apply_env_dump(env_dump(install['activationScript']), environ)
    print(f'+ activated ESP-IDF {install.get("name", "")} from {install.get("path", "")}', file=sys.stderr, flush=True)
    return venv_python(environ, install) or sys.executable


def main(argv: list[str] | None = None) -> int:
    """``--dump``: print the activation as KEY=VALUE lines (plus PYTHON=<venv interpreter>).

    The root CMakeLists.txt runs this when IDF_PATH is not in its environment, so a bare
    ``cmake --preset <board>`` (CLion, the command line) configures without an exported shell.
    """
    import argparse

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--dump', action='store_true', required=True)
    ap.parse_args(argv)

    env: dict[str, str] = dict(os.environ)
    python = activate(env)
    if python is None:
        print('no ESP-IDF install found: run the ESP-IDF export script first', file=sys.stderr)
        return 1
    for key, value in sorted(env.items()):
        if os.environ.get(key) != value:
            print(f'{key}={value}')
    print(f'PYTHON={python}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
