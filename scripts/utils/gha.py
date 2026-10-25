#!/usr/bin/env python3
"""GitHub Actions plumbing for the CI scripts in scripts/: workflow commands, step outputs, version check.

Stdlib only and old-syntax safe, so the version check can run before a too-old interpreter hits a SyntaxError.
"""

import os
import sys
from typing import NoReturn

# The scripts use 3.10+ syntax, as ESP-IDF's own tooling does.
MIN_PYTHON = (3, 10)

if sys.version_info < MIN_PYTHON:
    raise SystemExit(
        f'{os.path.basename(sys.argv[0]) or "this script"} requires Python >= '
        f'{".".join(map(str, MIN_PYTHON))}, got {sys.version.split()[0]}'
    )


def fail(msg: str) -> NoReturn:
    """Emit an Actions error annotation and stop the step."""
    error(msg)
    sys.exit(1)


def error(msg: str) -> None:
    print(f'::error::{msg}', flush=True)


def warn(msg: str) -> None:
    print(f'::warning::{msg}', flush=True)


def notice(msg: str) -> None:
    print(f'::notice::{msg}', flush=True)


def mask(value: str) -> None:
    """Register a value as a secret so the runner redacts it; call before it reaches any other output."""
    print(f'::add-mask::{value}', flush=True)


def set_output(name: str, value: str, *, secret: bool = False) -> None:
    """Write a step output (heredoc form for multi-line values); prints instead when GITHUB_OUTPUT is unset."""
    gh_output = os.environ.get('GITHUB_OUTPUT')
    if not gh_output:
        print(f'[output] {name}={"<redacted>" if secret else value}')
        return
    with open(gh_output, 'a', encoding='utf-8') as f:
        if '\n' in value:
            delim = f'__EOF_{name}__'
            f.write(f'{name}<<{delim}\n{value}\n{delim}\n')
        else:
            f.write(f'{name}={value}\n')


def summary(markdown: str) -> None:
    """Append to the job summary shown on the run page; a no-op outside Actions."""
    path = os.environ.get('GITHUB_STEP_SUMMARY')
    if path:
        with open(path, 'a', encoding='utf-8') as f:
            f.write(markdown + '\n')


def env(name: str, default: str = '') -> str:
    """An environment variable, stripped (a YAML folded scalar leaves a trailing newline)."""
    return os.environ.get(name, default).strip()


def require_env(*names: str) -> None:
    """Fail naming every missing variable at once, rather than one per re-run."""
    missing = [n for n in names if not env(n)]
    if missing:
        fail(f'Missing required environment variable(s): {", ".join(missing)}')
