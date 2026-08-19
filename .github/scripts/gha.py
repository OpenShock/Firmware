# !/usr/bin/env python3
"""Shared GitHub Actions plumbing: workflow commands, step outputs, version check.

Importing applies the version check.
Stdlib only, and conservative in syntax: this has to import on an interpreter too old to run its callers, or the check fails as a SyntaxError instead of the sentence explaining what to install.
"""

import os
import sys
from typing import NoReturn

# ESP-IDF's own tooling wants 3.9+; this floor is higher because these scripts use 3.12 typing syntax.
# The runners ship well past it - the check is here for the person running a script locally, not for CI.
MIN_PYTHON = (3, 12, 3)

if sys.version_info < MIN_PYTHON:
    raise SystemExit(
        '%s requires Python >= %s, got %s'
        % (
            os.path.basename(sys.argv[0]) or 'this script',
            '.'.join(map(str, MIN_PYTHON)),
            sys.version.split()[0],
        )
    )


def fail(msg: str) -> NoReturn:
    """Emit an Actions error annotation and stop the step."""
    print(f'::error::{msg}', flush=True)
    sys.exit(1)


def warn(msg: str) -> None:
    print(f'::warning::{msg}', flush=True)


def notice(msg: str) -> None:
    print(f'::notice::{msg}', flush=True)


def mask(value: str) -> None:
    """Register a value as a secret, so the runner redacts it from the log.

    Call before the value can reach any other output.
    """
    print(f'::add-mask::{value}', flush=True)


def set_output(name: str, value: str, *, secret: bool = False) -> None:
    """Write a step output, using the heredoc form when the value spans lines.

    Falls back to printing when GITHUB_OUTPUT is unset, so a script run locally says what it would have produced instead of failing on a missing environment.
    Pass secret=True for a credential: the local fallback then reports the name only, since there is no runner to apply the mask outside CI.
    """
    gh_output = os.environ.get('GITHUB_OUTPUT')
    if not gh_output:
        print(f"[output] {name}={'<redacted>' if secret else value}")
        return
    with open(gh_output, 'a', encoding='utf-8') as f:
        if '\n' in value:
            delim = f'__EOF_{name}__'
            f.write(f'{name}<<{delim}\n{value}\n{delim}\n')
        else:
            f.write(f'{name}={value}\n')


def env(name: str, default: str = '') -> str:
    """An environment variable, stripped.
    Actions inputs arrive with stray whitespace often enough - a YAML folded scalar leaves a trailing newline - that reading them raw is a bug waiting for the one input somebody wrote across two lines.
    """
    return os.environ.get(name, default).strip()


def require_env(*names: str) -> None:
    """Fail naming every missing variable at once, rather than one per re-run."""
    missing = [n for n in names if not env(n)]
    if missing:
        fail(f"Missing required environment variable(s): {', '.join(missing)}")
