"""What the GitHub Actions environment says about the build: whether it is CI, and for which ref."""

import os
from typing import NamedTuple

from . import channels

REF_KINDS = (('refs/tags/', 'tag'), ('refs/heads/', 'branch'), ('refs/pull/', 'pull'))


class Ref(NamedTuple):
    """A parsed GITHUB_REF. kind is 'tag', 'branch', 'pull', or '' for anything else (then name is the whole ref)."""

    kind: str
    name: str


def parse_ref(ref: str) -> Ref:
    """Ref('tag', '1.5.0'), Ref('branch', 'develop'), Ref('pull', '12') or Ref('', ref) for anything else."""
    for prefix, kind in REF_KINDS:
        if ref.startswith(prefix):
            return Ref(kind, ref.removeprefix(prefix).removesuffix('/merge'))
    return Ref('', ref)


def is_ci() -> bool:
    return os.environ.get('CI') == 'true' and os.environ.get('GITHUB_ACTIONS') == 'true'


def ref_name() -> str:
    """The branch or tag name the run is for, e.g. `develop` or `1.5.0-rc.1`."""
    return os.environ.get('GITHUB_REF_NAME', '')


def is_tag() -> bool:
    return os.environ.get('GITHUB_REF_TYPE') == 'tag'


def pr_base() -> str:
    """The branch a pull request targets; empty when the run is not for a pull request."""
    return os.environ.get('GITHUB_BASE_REF', '') if os.environ.get('GITHUB_EVENT_NAME') == 'pull_request' else ''


def build_channel() -> str | None:
    """The channel this run builds for: a tag's prerelease, else the branch (or the branch a pull request targets)."""
    ref = ref_name()
    if is_tag():
        return channels.prerelease_channel(ref.partition('-')[2])
    return channels.branch_channel(ref) or channels.branch_channel(pr_base())


def build_mode() -> tuple[str, int]:
    """(mode, log level) for the firmware: release for CI builds of master/beta and tags, beta's log level for beta and rc tags."""
    if not is_ci():
        return 'debug', 5
    channel = build_channel()
    if channel == 'beta':
        return 'release', 4
    if channel == 'stable' or is_tag():
        return 'release', 3
    return 'debug', 5


def build_optimization() -> str:
    """'size' (-Os) for CI builds of the stable channel, else 'debug' (-Og, ESP-IDF's default).

    Develop and beta stay at -Og so their backtraces and JTAG sessions keep every variable and line. A pull
    request into master builds as stable, so the optimized compile is checked before it is released.
    """
    return 'size' if is_ci() and build_channel() == 'stable' else 'debug'
