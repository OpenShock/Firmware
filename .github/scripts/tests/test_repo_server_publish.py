import pytest
import requests

import repo_server_publish


class FakeResponse:
    def __init__(self, status_code):
        self.status_code = status_code
        self.text = ''

    def json(self):
        return {}


class FakeSession:
    """Answers publish/delete calls; records which were made."""

    def __init__(self, publish):
        self.publish = publish  # a status code, or an exception to raise
        self.deleted = []

    def post(self, url, **kwargs):
        if isinstance(self.publish, Exception):
            raise self.publish
        return FakeResponse(self.publish)

    def delete(self, url, **kwargs):
        self.deleted.append(url)
        return FakeResponse(204)


@pytest.fixture(autouse=True)
def upload_env(monkeypatch):
    monkeypatch.setenv('RELEASE_ID', 'rel-1')
    monkeypatch.setenv('BOARDS', 'BoardA\nBoardB')
    monkeypatch.setattr(repo_server_publish, 'upload_board', lambda *args, **kwargs: None)
    monkeypatch.setattr(repo_server_publish, 'show_response', lambda resp: None)


def test_successful_publish_does_not_discard():
    session = FakeSession(publish=201)
    assert repo_server_publish.do_upload(session, 'https://repo', 'publish') == 0
    assert session.deleted == []


def test_rejected_publish_discards_the_release():
    session = FakeSession(publish=500)
    with pytest.raises(SystemExit):
        repo_server_publish.do_upload(session, 'https://repo', 'publish')
    assert len(session.deleted) == 1


def test_publish_without_a_response_is_not_discarded():
    # The release may already be live, so deleting it would be worse than leaving it for a human to check.
    session = FakeSession(publish=requests.ConnectionError('timed out'))
    with pytest.raises(SystemExit):
        repo_server_publish.do_upload(session, 'https://repo', 'publish')
    assert session.deleted == []


def test_dry_run_always_discards():
    session = FakeSession(publish=201)
    assert repo_server_publish.do_upload(session, 'https://repo', 'dry-run') == 0
    assert len(session.deleted) == 1
