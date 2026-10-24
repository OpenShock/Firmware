"""Firmware version strings: strict semver, plus the historical `v` prefix the repository's oldest tags carry."""

import semver


def parse_version(text: str) -> semver.Version:
    """Parse a version or tag such as `1.5.0-rc.2+abc` or `v0.8.1`. Raises ValueError naming the value."""
    try:
        return semver.Version.parse(text.strip().removeprefix('v'))
    except ValueError:
        raise ValueError(f'Invalid semver version: {text!r}') from None
