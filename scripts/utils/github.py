"""What the GitHub Actions environment says about the build: whether it is CI, and for which ref."""

import os


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


def build_mode() -> tuple[str, int]:
    """(mode, log level) for the firmware: release for CI builds of master/beta and tags, beta's log level for beta and rc tags."""
    if not is_ci():
        return 'debug', 5
    ref, base = ref_name(), pr_base()
    prerelease = ref.split('-', 1)[1].lower() if is_tag() and '-' in ref else ''

    if ref == 'beta' or base == 'beta' or prerelease.startswith(('rc', 'beta')):
        return 'release', 4
    if ref == 'master' or base == 'master' or is_tag():
        return 'release', 3
    return 'debug', 5
