"""The release channels the repository server knows, and which branch or tag publishes to each.

Shared by the CI scripts (get_vars.py, repo_server_policy.py, repo_server_publish.py) and by the
firmware's build mode (utils/github.py), so a channel is added or renamed in one place.
"""

# The channels the repository server's enum knows. A feature branch derives its own name, which is not one of them.
CHANNELS = ('stable', 'beta', 'develop')

# The branch each channel is cut from; a prod publish to the channel must be contained in it.
CHANNEL_BRANCH = {'stable': 'master', 'beta': 'beta', 'develop': 'develop'}
BRANCH_CHANNEL = {branch: channel for channel, branch in CHANNEL_BRANCH.items()}

# Channels whose prod publishes also need a staged draft release (develop builds have no release).
DRAFT_CHANNELS = ('stable', 'beta')

# A tag's first prerelease identifier names its channel: `1.5.0` is stable, `1.5.0-rc.1` beta.
PRERELEASE_CHANNEL = {
    '': 'stable',
    'stable': 'stable',
    'rc': 'beta',
    'beta': 'beta',
    'dev': 'develop',
    'develop': 'develop',
}


def prerelease_channel(prerelease: str | None) -> str | None:
    """The channel a version's prerelease (`rc.2`, '' or None when final) publishes to; None when it names none."""
    return PRERELEASE_CHANNEL.get((prerelease or '').split('.')[0].lower())


def branch_channel(branch: str) -> str | None:
    return BRANCH_CHANNEL.get(branch)
