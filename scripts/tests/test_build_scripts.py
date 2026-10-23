"""Tests for the build helpers in scripts/ (partition table reading, reproducible staticfs staging)."""

import gzip
import os
from pathlib import Path

import gen_staticfs
import pytest
from support import ROOT

from utils import partitions


def test_reads_the_repository_partition_table():
    table = partitions.read_partitions(ROOT / 'partitions' / 'ota_4mb.csv')

    assert table['static0']['offset'] == 0x353000
    assert table['static0']['size'] == 0x9D000
    assert table['config']['size'] == 0x3000
    assert partitions.first_app_partition(table)['offset'] == 0x10000


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
        gen_staticfs.stage_assets(build, staging)
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
    assert gen_staticfs.frontend_build_is_current(frontend, frontend / 'build')

    os.utime(source, (3000, 3000))
    assert not gen_staticfs.frontend_build_is_current(frontend, frontend / 'build')


def test_staged_assets_gzip_everything_but_prebuilt_gz(tmp_path):
    build = tmp_path / 'build'
    build.mkdir()
    (build / 'index.html').write_text('<title>x</title>', encoding='utf-8')
    (build / 'font.woff2.gz').write_bytes(b'already')

    www = gen_staticfs.stage_assets(build, tmp_path / 'staging')

    assert sorted(p.name for p in www.iterdir()) == ['font.woff2.gz', 'index.html.gz']
    assert (www / 'font.woff2.gz').read_bytes() == b'already'
    assert gzip.decompress((www / 'index.html.gz').read_bytes()) == b'<title>x</title>'


def test_staging_a_build_without_index_html_fails(tmp_path):
    build = tmp_path / 'build'
    build.mkdir()
    (build / 'app.js').write_text('1', encoding='utf-8')
    with pytest.raises(SystemExit, match='index.html.gz missing'):
        gen_staticfs.stage_assets(build, tmp_path / 'staging')


def test_packed_image_is_partition_sized_and_holds_only_www(tmp_path):
    from littlefs import LittleFS

    staging = tmp_path / 'staging'
    (staging / 'www').mkdir(parents=True)
    (staging / 'www' / 'index.html.gz').write_bytes(b'page')
    (staging / 'stray.txt').write_text('x', encoding='utf-8')
    output = tmp_path / 'out' / 'staticfs.bin'

    gen_staticfs.pack_image(staging, output, 64 * 1024)

    image = output.read_bytes()
    assert len(image) == 64 * 1024
    fs = LittleFS(block_size=gen_staticfs.BLOCK_SIZE, block_count=16, disk_version=0)
    fs.context.buffer[:] = image
    fs.mount()
    assert fs.listdir('/') == ['www']
    with fs.open('www/index.html.gz', 'rb') as f:
        assert f.read() == b'page'


def test_packing_refuses_a_size_that_is_not_a_block_multiple(tmp_path):
    with pytest.raises(SystemExit, match='not a multiple of block size'):
        gen_staticfs.pack_image(tmp_path, tmp_path / 'out.bin', 5000)
