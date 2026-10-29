"""Tests for the build helpers in scripts/ (partition table reading, reproducible www asset packing)."""

import gzip
import os

import gen_staticfs
import pytest
from support import ROOT

from utils import partitions, wwwimage


def test_reads_the_repository_partition_tables():
    table = partitions.read_partitions(ROOT / 'partitions' / 'ota_4mb.csv')
    assert table['www']['offset'] == 0x353000
    assert table['www']['size'] == 0x9D000
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


@pytest.mark.parametrize(
    'line, error',
    [
        ('nvs, data, nvs, , 0x5000', "partition 'nvs' in .* has no explicit offset"),
        ('nvs, data, nvs, 0x9zz0, 0x5000', "partition 'nvs' in .* has a bad offset '0x9zz0'"),
        ('nvs, data, nvs, 0x9000, 3G', "partition 'nvs' in .* has an unsupported size '3G'"),
        ('nvs, data, nvs, 0x9000', 'malformed partition line'),
    ],
)
def test_a_bad_entry_is_refused_naming_the_partition_and_file(tmp_path, line, error):
    csv = tmp_path / 'table.csv'
    csv.write_text(line + '\n', encoding='utf-8')
    with pytest.raises(SystemExit, match=error):
        partitions.read_partitions(csv)


def test_a_table_without_an_app_partition_is_refused():
    table = {'nvs': {'type': 'data', 'subtype': 'nvs', 'offset': 0x9000, 'size': 0x5000}}
    with pytest.raises(SystemExit, match='no app partition'):
        partitions.first_app_partition(table)


def test_the_www_partition_size_comes_from_the_table(tmp_path):
    csv = tmp_path / 'table.csv'
    csv.write_text('www, 0x40, 0x00, 0x3A8000, 0x48000\n', encoding='utf-8')
    assert gen_staticfs.www_partition_size(csv) == 0x48000
    csv.write_text('nvs, data, nvs, 0x9000, 0x5000\n', encoding='utf-8')
    with pytest.raises(SystemExit, match='has no www partition'):
        gen_staticfs.www_partition_size(csv)


def test_a_stale_frontend_without_pnpm_names_the_way_out(tmp_path, monkeypatch):
    monkeypatch.setattr(gen_staticfs.shutil, 'which', lambda name: None)
    with pytest.raises(SystemExit, match='pnpm not found.*--skip-frontend-build'):
        gen_staticfs.build_frontend(tmp_path)


def test_a_failing_pnpm_step_is_named(tmp_path, monkeypatch):
    def fail(cmd, **kwargs):
        raise gen_staticfs.subprocess.CalledProcessError(3, cmd)

    monkeypatch.setattr(gen_staticfs.shutil, 'which', lambda name: '/bin/pnpm')
    monkeypatch.setattr(gen_staticfs.subprocess, 'run', fail)
    with pytest.raises(SystemExit, match=r'`pnpm i` failed in .* \(exit 3\)'):
        gen_staticfs.build_frontend(tmp_path)


def write_build(tmp_path):
    build = tmp_path / 'build'
    (build / 'assets').mkdir(parents=True)
    (build / 'index.html').write_text('<!doctype html><title>x</title>', encoding='utf-8')
    (build / 'assets' / 'app.js').write_text('console.log(1)', encoding='utf-8')
    return build


def test_collected_assets_are_byte_identical_across_runs(tmp_path):
    build = write_build(tmp_path)

    first = gen_staticfs.collect_assets(build)
    # Touch the sources: gzip used to embed the mtime, so the output changed on every build.
    for p in build.rglob('*'):
        if p.is_file():
            p.touch()
    second = gen_staticfs.collect_assets(build)

    assert first == second
    assert sorted(e.path for e in first) == ['assets/app.js', 'index.html']
    assert all(e.gzip for e in first)
    assert wwwimage.pack(first) == wwwimage.pack(second)


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


def test_collected_assets_gzip_everything_but_prebuilt_gz(tmp_path):
    build = tmp_path / 'build'
    build.mkdir()
    (build / 'index.html').write_text('<title>x</title>', encoding='utf-8')
    (build / 'font.woff2.gz').write_bytes(b'already')
    (build / 'app.js').write_text('1', encoding='utf-8')
    (build / 'app.js.gz').write_bytes(b'prebuilt')

    entries = {e.path: e for e in gen_staticfs.collect_assets(build)}

    assert sorted(entries) == ['app.js', 'font.woff2', 'index.html']
    assert entries['font.woff2'].data == b'already'
    assert entries['app.js'].data == b'prebuilt'
    assert gzip.decompress(entries['index.html'].data) == b'<title>x</title>'


def test_immutable_assets_are_flagged(tmp_path):
    build = tmp_path / 'build'
    (build / '_app' / 'immutable').mkdir(parents=True)
    (build / 'index.html').write_text('x', encoding='utf-8')
    (build / '_app' / 'immutable' / 'chunk.abc123.js').write_text('1', encoding='utf-8')
    entries = {e.path: e for e in gen_staticfs.collect_assets(build)}
    assert entries['_app/immutable/chunk.abc123.js'].immutable
    assert not entries['index.html'].immutable


def test_collecting_a_build_without_index_html_fails(tmp_path):
    build = tmp_path / 'build'
    build.mkdir()
    (build / 'app.js').write_text('1', encoding='utf-8')
    with pytest.raises(SystemExit, match='index.html missing'):
        gen_staticfs.collect_assets(build)


def test_packed_image_is_the_bare_image_and_must_fit(tmp_path):
    entries = gen_staticfs.collect_assets(write_build(tmp_path))
    output = tmp_path / 'out' / 'www.bin'

    gen_staticfs.pack_image(entries, output, 64 * 1024)

    image = output.read_bytes()
    assert image == wwwimage.pack(entries)  # no padding to the partition size
    parsed = wwwimage.parse(image)
    assert gzip.decompress(parsed.find('index.html').data) == b'<!doctype html><title>x</title>'

    with pytest.raises(SystemExit, match='more than the 100-byte www partition'):
        gen_staticfs.pack_image(entries, output, 100)


def test_main_takes_the_smallest_of_every_limit(tmp_path, monkeypatch):
    build = write_build(tmp_path)
    csv = tmp_path / 'small.csv'
    csv.write_text('www, 0x40, 0x00, 0x3A8000, 0x1000\n', encoding='utf-8')
    limits = []
    monkeypatch.setattr(gen_staticfs, 'pack_image', lambda entries, output, max_size: limits.append(max_size))

    args = ['--frontend-dir', str(tmp_path), '--output', str(tmp_path / 'www.bin'), '--skip-frontend-build']
    gen_staticfs.main(
        [
            *args,
            '--max-size',
            '0x2000',
            '--partition-csv',
            str(csv),
            '--partition-csv',
            str(ROOT / 'partitions' / 'ota_4mb.csv'),
        ]
    )
    assert limits == [0x1000]
    assert build.is_dir()

    with pytest.raises(SystemExit):
        gen_staticfs.main(args)  # no limit at all

