"""Tests for the GitHub plumbing in scripts/utils: Actions step outputs and the REST API client."""

import pytest
from support import fails_with

from utils import gha, github_api


def test_set_output_writes_a_single_line_value(tmp_path, monkeypatch):
    output = tmp_path / 'output'
    monkeypatch.setenv('GITHUB_OUTPUT', str(output))
    gha.set_output('version', '1.5.0')
    gha.set_output('server', 'dev')
    assert output.read_text(encoding='utf-8') == 'version=1.5.0\nserver=dev\n'


def test_set_output_writes_a_multi_line_value_as_a_heredoc(tmp_path, monkeypatch):
    output = tmp_path / 'output'
    monkeypatch.setenv('GITHUB_OUTPUT', str(output))
    gha.set_output('board-list', 'A\nB')
    assert output.read_text(encoding='utf-8') == 'board-list<<__EOF_board-list__\nA\nB\n__EOF_board-list__\n'


def test_set_output_outside_actions_prints_and_redacts_secrets(capsys):
    gha.set_output('version', '1.5.0')
    gha.set_output('token', 'hunter2', secret=True)
    assert capsys.readouterr().out == '[output] version=1.5.0\n[output] token=<redacted>\n'


def test_summary_appends_and_is_a_no_op_outside_actions(tmp_path, monkeypatch):
    gha.summary('ignored')  # GITHUB_STEP_SUMMARY is unset
    path = tmp_path / 'summary'
    monkeypatch.setenv('GITHUB_STEP_SUMMARY', str(path))
    gha.summary('### One')
    gha.summary('### Two')
    assert path.read_text(encoding='utf-8') == '### One\n### Two\n'


def test_env_strips_a_folded_scalar(monkeypatch):
    monkeypatch.setenv('BOARDS', 'A B\n')
    assert gha.env('BOARDS') == 'A B'
    assert gha.env('NOT_SET_ANYWHERE', 'fallback') == 'fallback'


def test_require_env_names_every_missing_variable_at_once(monkeypatch, capsys):
    monkeypatch.setenv('PRESENT', 'x')
    with fails_with(capsys, r'Missing required environment variable\(s\): FIRST_MISSING, SECOND_MISSING'):
        gha.require_env('FIRST_MISSING', 'PRESENT', 'SECOND_MISSING')


class Response:
    def __init__(self, status_code, text='', body=None):
        self.status_code = status_code
        self.text = text
        self.body = body

    def json(self):
        return self.body


def test_github_get_sends_the_token_and_pinned_version(monkeypatch):
    sent = {}

    def get(url, **kwargs):
        sent.update(kwargs, url=url)
        return Response(200, body={'ok': True})

    monkeypatch.setattr(github_api.requests, 'get', get)
    assert github_api.github_get('/repos/x', 'tok', {'per_page': 1}) == {'ok': True}
    assert sent['url'] == 'https://api.github.com/repos/x'
    assert sent['params'] == {'per_page': 1}
    assert sent['headers']['Authorization'] == 'Bearer tok'
    assert sent['headers']['X-GitHub-Api-Version'] == github_api.API_VERSION
    assert sent['timeout']


def test_github_get_raises_with_the_status_and_a_truncated_body(monkeypatch):
    monkeypatch.setattr(github_api.requests, 'get', lambda url, **kwargs: Response(404, text='  ' + 'x' * 500))
    with pytest.raises(github_api.GitHubError, match=r'^HTTP 404: x{300}$') as exc:
        github_api.github_get('/repos/x', 'tok')
    assert exc.value.status == 404
