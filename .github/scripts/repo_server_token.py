#!/usr/bin/env python3
"""Mint a GitHub Actions OIDC token for the repository server.

Every call into the server goes through a token from here, so there is one definition
of the audience and one place where a malformed mint is caught.
"""

import base64
import json

import requests

from gha import env, fail, mask, require_env, set_output

# The claims the server keys off. Printed so a later rejection can be read against what
# was actually sent, rather than what the workflow assumes it sent. The token itself is
# masked; these five claims are not secret.
ECHOED_CLAIMS = ('aud', 'repository', 'repository_owner', 'ref', 'run_id')


def decode_claims(token: str) -> dict:
    """The payload segment of a JWT: base64url with its padding stripped."""
    try:
        payload = token.split('.')[1]
        raw = base64.urlsafe_b64decode(payload + '=' * (-len(payload) % 4))
        return json.loads(raw)
    except (IndexError, ValueError) as err:
        fail(f'OIDC token is not a readable JWT: {err}')


def main() -> int:
    require_env('AUDIENCE')
    audience = env('AUDIENCE')

    # id-token: write is granted per job, not per action, so a caller that forgets it
    # gets an unset request URL rather than a failed request. Say which it was.
    request_url = env('ACTIONS_ID_TOKEN_REQUEST_URL')
    if not request_url:
        fail('No OIDC request URL. The calling job needs permissions: id-token: write.')

    try:
        resp = requests.get(
            f'{request_url}&audience={audience}',
            headers={
                'Authorization': f"bearer {env('ACTIONS_ID_TOKEN_REQUEST_TOKEN')}",
                'Accept': 'application/json',
            },
            timeout=(10, 30),
        )
    except requests.RequestException as err:
        fail(f'Could not reach the OIDC token endpoint: {err}')

    if resp.status_code != 200:
        detail = (resp.text or '').strip()
        fail(f'GitHub refused the OIDC mint for audience {audience!r} (HTTP {resp.status_code}): {detail}')

    token = (resp.json().get('value') or '').strip()
    if not token:
        fail(f'GitHub returned no OIDC token for audience {audience!r}.')

    # Masked before it can reach an output file or a log line.
    mask(token)
    set_output('token', token, secret=True)

    claims = decode_claims(token)
    print(json.dumps({c: claims.get(c) for c in ECHOED_CLAIMS}, indent=2), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
