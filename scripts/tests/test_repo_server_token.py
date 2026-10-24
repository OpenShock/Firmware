"""Tests for scripts/repo_server_token.py: minting the OIDC token, masking it before anything can print it."""

import base64
import json

import pytest
import repo_server_token
import requests
from support import fails_with


def jwt(claims: dict) -> str:
    """A JWT-shaped string: base64url segments with their padding stripped, as GitHub issues them."""

    def segment(data: dict) -> str:
        return base64.urlsafe_b64encode(json.dumps(data).encode()).rstrip(b'=').decode()

    return f'{segment({"alg": "RS256"})}.{segment(claims)}.signature'


class Response:
    def __init__(self, status_code: int, body: dict | None = None, text: str = '') -> None:
        self.status_code = status_code
        self.body = body or {}
        self.text = text

    def json(self) -> dict:
        return self.body


@pytest.fixture
def oidc(monkeypatch):
    """The job's OIDC env, with requests.get answering the given response; returns the recorded calls."""
    calls = []
    monkeypatch.setenv('AUDIENCE', 'a&b c')
    monkeypatch.setenv('ACTIONS_ID_TOKEN_REQUEST_URL', 'https://oidc/token?api=1')
    monkeypatch.setenv('ACTIONS_ID_TOKEN_REQUEST_TOKEN', 'request-token')

    def answer(response):
        def get(url, **kwargs):
            calls.append((url, kwargs))
            if isinstance(response, Exception):
                raise response
            return response

        monkeypatch.setattr(repo_server_token.requests, 'get', get)

    return answer, calls


def test_audience_is_passed_as_a_query_parameter(oidc, capsys):
    answer, calls = oidc
    answer(Response(500))
    with fails_with(capsys, 'HTTP 500'):
        repo_server_token.main()
    assert calls[0][0] == 'https://oidc/token?api=1'
    assert calls[0][1]['params'] == {'audience': 'a&b c'}
    assert calls[0][1]['headers']['Authorization'] == 'bearer request-token'


def test_the_token_is_masked_before_it_is_output(oidc, monkeypatch, tmp_path, capsys):
    answer, _ = oidc
    token = jwt({'aud': 'a&b c', 'repository': 'OpenShock/Firmware', 'sub': 'secret-ish'})
    answer(Response(200, {'value': token}))
    output = tmp_path / 'output'
    monkeypatch.setenv('GITHUB_OUTPUT', str(output))

    assert repo_server_token.main() == 0

    lines = capsys.readouterr().out.splitlines()
    # The mask is the first line to mention the token, and no other line does.
    assert lines[0] == f'::add-mask::{token}'
    assert not any(token in line for line in lines[1:])
    assert output.read_text(encoding='utf-8') == f'token={token}\n'
    # Only the echoed claims are printed.
    echoed = json.loads('\n'.join(lines[1:]))
    assert echoed['aud'] == 'a&b c' and echoed['repository'] == 'OpenShock/Firmware'
    assert 'sub' not in echoed


def test_without_a_request_url_the_missing_permission_is_named(oidc, monkeypatch, capsys):
    monkeypatch.delenv('ACTIONS_ID_TOKEN_REQUEST_URL')
    with fails_with(capsys, 'id-token: write'):
        repo_server_token.main()


def test_an_unreachable_endpoint_is_reported(oidc, capsys):
    answer, _ = oidc
    answer(requests.ConnectionError('refused'))
    with fails_with(capsys, 'Could not reach the OIDC token endpoint: refused'):
        repo_server_token.main()


def test_an_empty_token_is_refused(oidc, capsys):
    answer, _ = oidc
    answer(Response(200, {'value': '  '}))
    with fails_with(capsys, "returned no OIDC token for audience 'a&b c'"):
        repo_server_token.main()


def test_decode_claims_reads_an_unpadded_payload():
    assert repo_server_token.decode_claims(jwt({'run_id': '7'})) == {'run_id': '7'}


@pytest.mark.parametrize('token', ['no-dots-at-all', 'header.!!!not-base64!!!.sig'])
def test_decode_claims_refuses_what_is_not_a_jwt(capsys, token):
    with fails_with(capsys, 'OIDC token is not a readable JWT'):
        repo_server_token.decode_claims(token)
