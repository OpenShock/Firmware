"""Tests for scripts/repo_server_publish.py: staging, uploading, promoting and discarding a release, server faked."""

import hashlib
import json

import pytest
import repo_server_publish
import requests
from support import fails_with

_REAL_UPLOAD_BOARD = repo_server_publish.upload_board


class FakeResponse:
    def __init__(self, status_code, body=None):
        self.status_code = status_code
        self.text = ''
        self.body = body if body is not None else {}

    def json(self):
        return self.body


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
    assert repo_server_publish.do_publish(session, 'https://repo') == 0
    assert session.deleted == []


def test_rejected_publish_discards_the_release(capsys):
    session = FakeSession(publish=500)
    with fails_with(capsys, r'Publish failed \(HTTP 500\)'):
        repo_server_publish.do_publish(session, 'https://repo')
    assert len(session.deleted) == 1


def test_publish_without_a_response_is_not_discarded(tmp_path, monkeypatch, capsys):
    # The release may already be live, so deleting it would be worse than leaving it for a human to check.
    output = tmp_path / 'output'
    monkeypatch.setenv('GITHUB_OUTPUT', str(output))
    session = FakeSession(publish=requests.ConnectionError('timed out'))
    with fails_with(capsys, 'Not discarding rel-1'):
        repo_server_publish.do_publish(session, 'https://repo')
    assert session.deleted == []
    assert 'outcome-unknown=true' in output.read_text(encoding='utf-8')


def test_publish_without_boards_is_refused(monkeypatch, capsys):
    monkeypatch.setenv('BOARDS', '')
    with fails_with(capsys, 'BOARDS is empty; there is nothing to upload'):
        repo_server_publish.do_publish(FakeSession(publish=201), 'https://repo')


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


def test_resolve_artifacts_fails_on_a_missing_image(tmp_path, capsys):
    write_board_artifacts(tmp_path, 'BoardA')
    (tmp_path / 'firmware_boards' / 'BoardA' / 'bootloader.bin').unlink()
    with fails_with(capsys, 'BoardA is missing its bootloader artifact'):
        repo_server_publish.resolve_artifacts(tmp_path, 'BoardA')


def test_resolve_artifacts_fails_without_a_merged_image(tmp_path, capsys):
    write_board_artifacts(tmp_path, 'BoardA')
    for merged in (tmp_path / 'firmware_merged').iterdir():
        merged.unlink()
    with fails_with(capsys, 'BoardA is missing its merged artifact'):
        repo_server_publish.resolve_artifacts(tmp_path, 'BoardA')


@pytest.mark.parametrize(
    'status, expected', [(204, 'discarded'), (404, 'already_closed'), (409, 'already_closed'), (500, 'failed')]
)
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


# --- upload_board, init_release, extract_changelog ---


class ScriptedSession:
    """Plays back one scripted outcome per call: a status code, or an exception to raise."""

    def __init__(self, *outcomes):
        self.outcomes = list(outcomes)
        self.calls = []

    def _answer(self, url, kwargs):
        self.calls.append((url, kwargs))
        outcome = self.outcomes.pop(0)
        if isinstance(outcome, Exception):
            raise outcome
        return FakeResponse(outcome)

    put = post = delete = lambda self, url, **kwargs: self._answer(url, kwargs)


@pytest.fixture
def real_upload(monkeypatch, tmp_path):
    monkeypatch.setattr(repo_server_publish, 'upload_board', _REAL_UPLOAD_BOARD)
    monkeypatch.setattr(repo_server_publish.time, 'sleep', lambda seconds: None)
    write_board_artifacts(tmp_path, 'BoardA')
    return tmp_path


def test_upload_retries_a_5xx_then_succeeds(real_upload):
    session = ScriptedSession(503, requests.ConnectionError('reset'), 200)
    repo_server_publish.upload_board(session, 'https://repo', 'rel-1', 'BoardA', real_upload)
    assert [url for url, _ in session.calls] == ['https://repo/2/firmware/releases/rel-1/boards/BoardA'] * 3


def test_upload_gives_up_after_the_last_5xx(real_upload, capsys):
    session = ScriptedSession(500, 500, 500)
    with fails_with(capsys, r'Upload failed for BoardA \(HTTP 500\)'):
        repo_server_publish.upload_board(session, 'https://repo', 'rel-1', 'BoardA', real_upload)
    assert len(session.calls) == repo_server_publish.UPLOAD_RETRIES


def test_upload_does_not_retry_a_4xx(real_upload, capsys):
    session = ScriptedSession(422)
    with fails_with(capsys, r'Upload failed for BoardA \(HTTP 422\)'):
        repo_server_publish.upload_board(session, 'https://repo', 'rel-1', 'BoardA', real_upload)
    assert len(session.calls) == 1


def test_upload_gives_up_after_repeated_transport_errors(real_upload, capsys):
    session = ScriptedSession(*[requests.ConnectionError('down')] * repo_server_publish.UPLOAD_RETRIES)
    with fails_with(capsys, 'Upload failed for BoardA after 3 attempts: down'):
        repo_server_publish.upload_board(session, 'https://repo', 'rel-1', 'BoardA', real_upload)
    assert not session.outcomes


def test_upload_sends_a_sha256_manifest_of_every_artifact(real_upload):
    session = ScriptedSession(200)
    repo_server_publish.upload_board(session, 'https://repo', 'rel-1', 'BoardA', real_upload)
    kwargs = session.calls[0][1]
    manifest = json.loads(kwargs['data']['sha256'])
    assert set(manifest) == set(kwargs['files']) == {*repo_server_publish.BUILD_ARTIFACTS, 'staticfs', 'merged'}
    assert manifest['merged'] == hashlib.sha256(b'merged').hexdigest()


class CreatedResponse(FakeResponse):
    def json(self):
        return {'id': 'rel-9', 'status': 'staged'}


def test_init_release_returns_the_staged_id():
    session = FakeSession(publish=201)
    session.post = lambda url, **kwargs: CreatedResponse(201)
    release_id = repo_server_publish.init_release(
        session, 'https://repo', version='1.5.0', channel='beta', boards=['BoardA'], changelog='# 1.5.0'
    )
    assert release_id == 'rel-9'


def test_init_release_dies_on_anything_but_201(capsys):
    session = FakeSession(publish=200)
    session.post = lambda url, **kwargs: FakeResponse(409)
    with fails_with(capsys, r'Init failed \(HTTP 409\)'):
        repo_server_publish.init_release(
            session, 'https://repo', version='1.5.0', channel='beta', boards=['BoardA'], changelog='x'
        )


def test_extract_changelog_takes_the_newest_section_only(tmp_path):
    changelog = tmp_path / 'CHANGELOG.md'
    changelog.write_text('preamble\n# 1.5.0\n- new\n\n## Fixes\n- a\n# 1.4.0\n- old\n', encoding='utf-8')
    assert repo_server_publish.extract_changelog(changelog) == '# 1.5.0\n- new\n\n## Fixes\n- a'


@pytest.mark.parametrize(
    'body, error',
    [
        (None, "Changelog file '.*' not found"),
        ('no heading here\n', "No '# ' heading found"),
        ('## only a subheading\n', "No '# ' heading found"),
    ],
)
def test_extract_changelog_fails_without_a_section(tmp_path, capsys, body, error):
    changelog = tmp_path / 'CHANGELOG.md'
    if body is not None:
        changelog.write_text(body, encoding='utf-8')
    with fails_with(capsys, error):
        repo_server_publish.extract_changelog(changelog)


@pytest.mark.parametrize('status, closed', [(404, True), (409, True), (500, False)])
def test_abort_mode_reports_but_never_fails(monkeypatch, capsys, status, closed):
    monkeypatch.setenv('RELEASE_ID', 'rel-1')
    session = ScriptedSession(status)
    assert repo_server_publish.do_abort(session, 'https://repo') == 0
    assert ('already closed' in capsys.readouterr().out) == closed


def test_abort_mode_without_a_release_does_nothing(monkeypatch):
    monkeypatch.setenv('RELEASE_ID', '')
    session = ScriptedSession()
    assert repo_server_publish.do_abort(session, 'https://repo') == 0
    assert session.calls == []


# --- init mode, MODE dispatch, a publish over real artifacts ---


@pytest.fixture
def in_checkout(tmp_path, monkeypatch):
    """Runs in a stand-in checkout: CHANGELOG.md and artifacts/ for two boards, relative to the working directory."""
    monkeypatch.chdir(tmp_path)
    (tmp_path / 'CHANGELOG.md').write_text('# 1.5.0\n- new\n# 1.4.0\n', encoding='utf-8')
    for board in ('BoardA', 'BoardB'):
        write_board_artifacts(tmp_path / 'artifacts', board)
    output = tmp_path / 'output'
    monkeypatch.setenv('GITHUB_OUTPUT', str(output))
    return output


def test_init_stages_the_release_and_outputs_its_id(in_checkout, monkeypatch):
    monkeypatch.setenv('VERSION', '1.5.0')
    monkeypatch.setenv('CHANNEL', 'beta')
    session = ScriptedSession()
    sent = {}

    def post(url, **kwargs):
        sent.update(kwargs['json'], url=url)
        return FakeResponse(201, {'id': 'rel-9', 'status': 'staged'})

    session.post = post
    assert repo_server_publish.do_init(session, 'https://repo') == 0
    assert sent['url'] == 'https://repo/2/firmware/releases'
    assert (sent['version'], sent['channel'], sent['boards']) == ('1.5.0', 'beta', ['BoardA', 'BoardB'])
    assert sent['changelog'] == '# 1.5.0\n- new'
    assert 'release-id=rel-9' in in_checkout.read_text(encoding='utf-8')


@pytest.mark.parametrize(
    'env, error',
    [
        ({'VERSION': '1.5.0', 'CHANNEL': 'nightly'}, "CHANNEL must be one of stable/beta/develop, got 'nightly'"),
        ({'VERSION': '1.5.0', 'CHANNEL': 'beta', 'BOARDS': ''}, 'BOARDS is empty; there is nothing to declare'),
        ({'CHANNEL': 'beta'}, r'Missing required environment variable\(s\): VERSION'),
    ],
)
def test_init_refuses_what_the_server_would(in_checkout, monkeypatch, capsys, env, error):
    for name, value in env.items():
        monkeypatch.setenv(name, value)
    with fails_with(capsys, error):
        repo_server_publish.do_init(ScriptedSession(), 'https://repo')


def test_publish_uploads_every_board_then_promotes(in_checkout, monkeypatch):
    monkeypatch.setattr(repo_server_publish, 'upload_board', _REAL_UPLOAD_BOARD)
    session = ScriptedSession(200, 200, 201)
    assert repo_server_publish.do_publish(session, 'https://repo') == 0
    assert [url for url, _ in session.calls] == [
        'https://repo/2/firmware/releases/rel-1/boards/BoardA',
        'https://repo/2/firmware/releases/rel-1/boards/BoardB',
        'https://repo/2/firmware/releases/rel-1/publish',
    ]


@pytest.mark.parametrize('mode, handler', [('init', 'do_init'), ('publish', 'do_publish'), ('abort', 'do_abort')])
def test_main_dispatches_on_mode(monkeypatch, mode, handler):
    called = []
    monkeypatch.setenv('SERVER_URL', 'https://repo/')
    monkeypatch.setenv('TOKEN', 't')
    monkeypatch.setenv('MODE', mode)
    monkeypatch.setattr(repo_server_publish, handler, lambda session, server: called.append(server) or 0)
    assert repo_server_publish.main() == 0
    assert called == ['https://repo']


def test_main_refuses_an_unknown_mode(monkeypatch, capsys):
    monkeypatch.setenv('SERVER_URL', 'https://repo')
    monkeypatch.setenv('TOKEN', 't')
    monkeypatch.setenv('MODE', 'upload')
    with fails_with(capsys, "MODE must be one of init/publish/abort, got 'upload'"):
        repo_server_publish.main()
