import pytest
import repo_server_publish
import requests


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
    assert repo_server_publish.do_upload(session, 'https://repo') == 0
    assert session.deleted == []


def test_rejected_publish_discards_the_release():
    session = FakeSession(publish=500)
    with pytest.raises(SystemExit):
        repo_server_publish.do_upload(session, 'https://repo')
    assert len(session.deleted) == 1


def test_publish_without_a_response_is_not_discarded(tmp_path, monkeypatch):
    # The release may already be live, so deleting it would be worse than leaving it for a human to check.
    output = tmp_path / 'output'
    monkeypatch.setenv('GITHUB_OUTPUT', str(output))
    session = FakeSession(publish=requests.ConnectionError('timed out'))
    with pytest.raises(SystemExit):
        repo_server_publish.do_upload(session, 'https://repo')
    assert session.deleted == []
    assert 'outcome-unknown=true' in output.read_text()


# --- artifact layout ---


def write_board_artifacts(artifacts, board, version='1.5.0'):
    build = artifacts / 'firmware_boards' / board
    build.mkdir(parents=True)
    for filename in repo_server_publish.BUILD_ARTIFACTS.values():
        (build / filename).write_bytes(filename.encode())
    (artifacts / 'firmware_staticfs').mkdir(exist_ok=True)
    (artifacts / 'firmware_staticfs' / 'staticfs.bin').write_bytes(b'fs')
    (artifacts / 'firmware_merged').mkdir(exist_ok=True)
    (artifacts / 'firmware_merged' / f'OpenShock_{board}_{version}.bin').write_bytes(b'merged')


def test_resolve_artifacts_reads_the_package_layout(tmp_path):
    write_board_artifacts(tmp_path, 'BoardA')
    write_board_artifacts(tmp_path, 'BoardA_Mini')

    found = repo_server_publish.resolve_artifacts(tmp_path, 'BoardA')

    for field, filename in repo_server_publish.BUILD_ARTIFACTS.items():
        assert found[field] == tmp_path / 'firmware_boards' / 'BoardA' / filename
    assert found[repo_server_publish.STATICFS_FIELD] == tmp_path / 'firmware_staticfs' / 'staticfs.bin'
    # A board whose name extends another's does not pick up the other's merged image.
    assert found[repo_server_publish.MERGED_FIELD] == tmp_path / 'firmware_merged' / 'OpenShock_BoardA_1.5.0.bin'


def test_resolve_artifacts_fails_on_a_missing_image(tmp_path):
    write_board_artifacts(tmp_path, 'BoardA')
    (tmp_path / 'firmware_boards' / 'BoardA' / 'bootloader.bin').unlink()
    with pytest.raises(SystemExit):
        repo_server_publish.resolve_artifacts(tmp_path, 'BoardA')


@pytest.mark.parametrize('status, expected', [(204, 'discarded'), (404, 'already_closed'), (409, 'already_closed'), (500, 'failed')])
def test_discard_reports_what_happened(status, expected):
    session = FakeSession(publish=201)
    session.delete = lambda url, **kwargs: FakeResponse(status)
    assert repo_server_publish.discard(session, 'https://repo', 'rel-1') == expected


def test_discard_without_a_response_fails_quietly():
    def unreachable(url, **kwargs):
        raise requests.ConnectionError('down')

    session = FakeSession(publish=201)
    session.delete = unreachable
    assert repo_server_publish.discard(session, 'https://repo', 'rel-1') == 'failed'
