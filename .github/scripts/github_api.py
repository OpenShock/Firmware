"""GitHub REST API reads shared by the gate and the publishing policy."""

from typing import Any

import requests

API = 'https://api.github.com'
# Pinned so a new default cannot change what the checks see; the 400 for an unknown version lists the supported ones.
API_VERSION = '2026-03-10'


class GitHubError(Exception):
    def __init__(self, status: int, detail: str) -> None:
        super().__init__(f'HTTP {status}: {detail}')
        self.status = status


def github_get(path: str, token: str, params: dict | None = None) -> Any:
    """GET a JSON document; raises GitHubError on a non-200 and requests.RequestException on transport errors."""
    resp = requests.get(
        f'{API}{path}',
        params=params,
        headers={
            'Authorization': f'Bearer {token}',
            'Accept': 'application/vnd.github+json',
            'X-GitHub-Api-Version': API_VERSION,
        },
        timeout=(10, 30),
    )
    if resp.status_code != 200:
        raise GitHubError(resp.status_code, (resp.text or '').strip()[:300])
    return resp.json()
