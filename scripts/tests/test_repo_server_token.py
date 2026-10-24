import pytest
import repo_server_token


def test_audience_is_passed_as_a_query_parameter(monkeypatch):
    calls = []

    class Response:
        status_code = 500
        text = ''

    monkeypatch.setenv('AUDIENCE', 'a&b c')
    monkeypatch.setenv('ACTIONS_ID_TOKEN_REQUEST_URL', 'https://oidc/token?api=1')
    monkeypatch.setattr(repo_server_token.requests, 'get', lambda url, **kwargs: calls.append((url, kwargs)) or Response())

    with pytest.raises(SystemExit):
        repo_server_token.main()

    assert calls[0][0] == 'https://oidc/token?api=1'
    assert calls[0][1]['params'] == {'audience': 'a&b c'}
