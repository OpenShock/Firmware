"""Tests for the build helpers in scripts/ (partition table reading, reproducible staticfs staging)."""

import os
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts'))

import gen_staticfs  # noqa: E402
from utils import partitions  # noqa: E402


def test_reads_the_repository_partition_table():
    table = partitions.read_partitions(ROOT / 'partitions' / 'ota_4mb.csv')

    assert table['static0']['offset'] == 0x353000
    assert table['static0']['size'] == 0x9D000
    assert table['config']['size'] == 0x3000
    assert partitions.first_app_partition(table)['offset'] == 0x10000


def test_every_board_resolves_a_partition_table():
    for fragment in sorted((ROOT / 'boards').glob('*.defaults')):
        board = fragment.stem
        csv = partitions.partition_csv_for(board)
        assert csv.is_file(), board
        assert 'static0' in partitions.read_partitions(csv), board
        assert partitions.partition_table_offset(board) == 0x8000, board


def test_sizes_with_suffixes_and_comments(tmp_path):
    csv = tmp_path / 'table.csv'
    csv.write_text(
        '# Name, Type, SubType, Offset, Size\n'
        'nvs, data, nvs, 0x9000, 20K  # trailing comment\n'
        'app0, app, ota_0, 0x10000, 1M\n',
        encoding='utf-8',
    )
    table = partitions.read_partitions(csv)
    assert table['nvs']['size'] == 20 * 1024
    assert table['app0']['size'] == 1024 * 1024


def test_entries_without_an_offset_are_refused(tmp_path):
    csv = tmp_path / 'table.csv'
    csv.write_text('nvs, data, nvs, , 0x5000\n', encoding='utf-8')
    with pytest.raises(SystemExit):
        partitions.read_partitions(csv)


def test_staged_assets_are_byte_identical_across_runs(tmp_path):
    build = tmp_path / 'build'
    (build / 'assets').mkdir(parents=True)
    (build / 'index.html').write_text('<!doctype html><title>x</title>', encoding='utf-8')
    (build / 'assets' / 'app.js').write_text('console.log(1)', encoding='utf-8')

    def stage(name):
        staging = tmp_path / name
        gen_staticfs.stage_assets(str(build), str(staging))
        return {p.relative_to(staging): p.read_bytes() for p in staging.rglob('*') if p.is_file()}

    first = stage('a')
    # Touch the sources: gzip used to embed the mtime, so the output changed on every build.
    for p in build.rglob('*'):
        if p.is_file():
            p.touch()
    second = stage('b')

    assert first == second
    assert Path('www') / 'index.html.gz' in first


def test_frontend_build_is_current_tracks_source_changes(tmp_path):
    frontend = tmp_path / 'frontend'
    (frontend / 'src').mkdir(parents=True)
    (frontend / 'build').mkdir()
    source = frontend / 'src' / 'main.ts'
    index = frontend / 'build' / 'index.html'

    source.write_text('a', encoding='utf-8')
    index.write_text('built', encoding='utf-8')

    os.utime(source, (1000, 1000))
    os.utime(index, (2000, 2000))
    assert gen_staticfs.frontend_build_is_current(str(frontend), str(frontend / 'build'))

    os.utime(source, (3000, 3000))
    assert not gen_staticfs.frontend_build_is_current(str(frontend), str(frontend / 'build'))
