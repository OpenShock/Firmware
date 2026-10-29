"""Tests for scripts/fetch_flatc.py and scripts/generate_schemas.py: picking, verifying and running flatc."""

import hashlib
import io
import os
import subprocess
import zipfile

import fetch_flatc
import generate_schemas
import pytest


class FakeResponse:
    def __init__(self, body=None, content=b''):
        self.body = body
        self.content = content

    def raise_for_status(self):
        pass

    def json(self):
        return self.body

    def iter_content(self, chunk_size):
        for i in range(0, len(self.content), chunk_size):
            yield self.content[i : i + chunk_size]

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


# --- the submodule's version, and its release ---


def test_submodule_version_reads_base_h(tmp_path, monkeypatch):
    base_h = tmp_path / 'base.h'
    base_h.write_text(
        '#define FLATBUFFERS_VERSION_MAJOR 25\n#define FLATBUFFERS_VERSION_MINOR 12\n#define FLATBUFFERS_VERSION_REVISION 19\n',
        encoding='utf-8',
    )
    monkeypatch.setattr(fetch_flatc, 'BASE_H', base_h)
    assert fetch_flatc.submodule_version() == '25.12.19'


def test_an_uninitialised_submodule_names_the_fix(tmp_path, monkeypatch):
    monkeypatch.setattr(fetch_flatc, 'BASE_H', tmp_path / 'missing' / 'base.h')
    with pytest.raises(SystemExit, match='git submodule update --init'):
        fetch_flatc.submodule_version()


def test_a_base_h_without_a_version_is_refused(tmp_path, monkeypatch):
    base_h = tmp_path / 'base.h'
    base_h.write_text('#define FLATBUFFERS_VERSION_MAJOR 25\n', encoding='utf-8')
    monkeypatch.setattr(fetch_flatc, 'BASE_H', base_h)
    with pytest.raises(SystemExit, match='FLATBUFFERS_VERSION_MINOR not found'):
        fetch_flatc.submodule_version()


@pytest.fixture
def release_pages(monkeypatch):
    """Serves the releases API from a list of pages; records the page numbers asked for."""
    pages: list[list[dict]] = []
    asked: list[int] = []

    def get(url, params, timeout):
        asked.append(params['page'])
        index = params['page'] - 1
        return FakeResponse(pages[index] if index < len(pages) else [])

    monkeypatch.setattr(fetch_flatc.requests, 'get', get)
    return pages, asked


def test_the_release_is_found_by_tag_across_pages(release_pages):
    pages, asked = release_pages
    pages += [[{'tag_name': 'v25.12.190'}, {'tag_name': 'v25.12.1'}], [{'tag_name': 'v25.12.19-2026-02-06-03fffb2'}]]
    assert fetch_flatc.release_for_version('25.12.19')['tag_name'] == 'v25.12.19-2026-02-06-03fffb2'
    assert asked == [1, 2]


def test_no_matching_release_stops_at_the_empty_page(release_pages):
    pages, asked = release_pages
    pages.append([{'tag_name': 'v24.3.25'}])
    with pytest.raises(SystemExit, match='no flatbuffers release found for version 25.12.19'):
        fetch_flatc.release_for_version('25.12.19')
    assert asked == [1, 2]


# --- verified downloads ---


def asset(content: bytes, digest: str | None = 'auto') -> dict:
    found = {'name': 'Linux.flatc.binary.clang++-18.zip', 'browser_download_url': 'https://example/flatc.zip'}
    if digest == 'auto':
        found['digest'] = 'sha256:' + hashlib.sha256(content).hexdigest()
    elif digest is not None:
        found['digest'] = digest
    return found


def serve(monkeypatch, content: bytes) -> list[str]:
    fetched: list[str] = []

    def get(url, stream, timeout):
        fetched.append(url)
        return FakeResponse(content=content)

    monkeypatch.setattr(fetch_flatc.requests, 'get', get)
    return fetched


def test_a_download_matching_its_digest_is_kept(tmp_path, monkeypatch):
    serve(monkeypatch, b'zipbytes' * 3000)
    dest = tmp_path / 'flatc.zip'
    fetch_flatc.download_verified(asset(b'zipbytes' * 3000), dest)
    assert dest.read_bytes() == b'zipbytes' * 3000


def test_a_download_not_matching_its_digest_is_refused(tmp_path, monkeypatch):
    serve(monkeypatch, b'tampered')
    with pytest.raises(SystemExit, match='SHA-256 mismatch'):
        fetch_flatc.download_verified(asset(b'original'), tmp_path / 'flatc.zip')


def test_an_asset_without_a_digest_is_not_even_downloaded(tmp_path, monkeypatch):
    fetched = serve(monkeypatch, b'zip')
    with pytest.raises(SystemExit, match='published no digest'):
        fetch_flatc.download_verified(asset(b'zip', digest=None), tmp_path / 'flatc.zip')
    assert fetched == []


def test_a_release_without_the_asset_is_refused():
    with pytest.raises(SystemExit, match="no asset matching 'Windows.flatc.binary' in flatbuffers release v1"):
        fetch_flatc.find_asset(
            {'tag_name': 'v1', 'assets': [{'name': 'Linux.flatc.binary.zip'}]}, 'Windows.flatc.binary'
        )


@pytest.mark.skipif(os.name != 'posix', reason='Windows has no executable bit')
def test_extracted_flatc_is_executable(tmp_path, monkeypatch):
    archive = io.BytesIO()
    with zipfile.ZipFile(archive, 'w') as zf:
        zf.writestr('flatc', b'#!/bin/sh\n')
    content = archive.getvalue()
    serve(monkeypatch, content)
    release = {'tag_name': 'v1', 'assets': [{**asset(content), 'name': marker} for marker in fetch_flatc.ASSET_MARKERS]}

    fetch_flatc.fetch_binaries(release, tmp_path)

    assert os.access(tmp_path / 'flatc', os.X_OK)


# --- generate_schemas: finding flatc, swapping the generated tree ---


def test_flatc_problem_explains_a_flatc_that_is_not_executable(monkeypatch):
    def denied(cmd, **kwargs):
        raise PermissionError(13, 'Permission denied', cmd[0])

    monkeypatch.setattr(generate_schemas.subprocess, 'run', denied)
    assert 'not executable' in generate_schemas.flatc_problem('flatc')


def test_find_flatc_prefers_the_pinned_copy_and_reports_both_failures(monkeypatch):
    problems = {
        str(generate_schemas.SCRIPTS_DIR / generate_schemas.FLATC_NAME): 'not found',
        generate_schemas.FLATC_NAME: None,
    }
    monkeypatch.setattr(generate_schemas, 'flatc_problem', lambda flatc: problems[str(flatc)])
    assert generate_schemas.find_flatc() == generate_schemas.FLATC_NAME

    problems[generate_schemas.FLATC_NAME] = 'not found'
    with pytest.raises(SystemExit, match='(?s)no working flatc.*not found.*not found.*fetch_flatc.py'):
        generate_schemas.find_flatc()


@pytest.fixture
def fake_flatc(monkeypatch):
    """A flatc that writes one file into its -o directory, or fails when told to."""
    state = {'fail': False}

    def run(cmd, check):
        if state['fail']:
            raise subprocess.CalledProcessError(1, cmd)
        out = cmd[cmd.index('-o') + 1]
        (generate_schemas.Path(out) / 'generated.h').write_text('new', encoding='utf-8')

    monkeypatch.setattr(generate_schemas.subprocess, 'run', run)
    return state


def test_generate_replaces_the_old_tree(tmp_path, fake_flatc):
    output = tmp_path / '_fbs'
    output.mkdir()
    (output / 'stale.h').write_text('old', encoding='utf-8')

    generate_schemas.generate('flatc', ['--cpp'], ['a.fbs'], output, 'C++')

    assert [p.name for p in output.iterdir()] == ['generated.h']
    assert [p.name for p in tmp_path.iterdir()] == ['_fbs']  # nothing staged is left behind


def test_a_failing_flatc_leaves_the_old_tree(tmp_path, fake_flatc):
    output = tmp_path / '_fbs'
    output.mkdir()
    (output / 'stale.h').write_text('old', encoding='utf-8')
    fake_flatc['fail'] = True

    with pytest.raises(SystemExit, match='flatc failed for C\\+\\+ .* left unchanged'):
        generate_schemas.generate('flatc', ['--cpp'], ['a.fbs'], output, 'C++')

    assert (output / 'stale.h').read_text(encoding='utf-8') == 'old'
    assert [p.name for p in tmp_path.iterdir()] == ['_fbs']


def test_a_failed_swap_restores_the_old_tree(tmp_path, fake_flatc, monkeypatch):
    output = tmp_path / '_fbs'
    output.mkdir()
    (output / 'stale.h').write_text('old', encoding='utf-8')
    real_rename = generate_schemas.Path.rename

    def rename(self, target):
        if self.name == 'new':
            raise PermissionError(13, 'locked by an editor')
        return real_rename(self, target)

    monkeypatch.setattr(generate_schemas.Path, 'rename', rename)
    with pytest.raises(PermissionError):
        generate_schemas.generate('flatc', ['--cpp'], ['a.fbs'], output, 'C++')
    assert (output / 'stale.h').read_text(encoding='utf-8') == 'old'
