"""Tests for the command line of scripts/merge_image.py; esptool itself is never run."""

import merge_image
import pytest

ARGS = ['--board', 'OpenShock-Core-V2', '--staticfs', 'staticfs.bin']


@pytest.mark.parametrize(
    'bad',
    [
        ['--bindir', 'boards/{variant}', '--output', 'merged/{board}.bin'],
        ['--bindir', 'boards/{board}', '--output', 'merged/{version}.bin'],
        ['--bindir', 'boards/{board}', '--output', 'merged/{}.bin'],
    ],
)
def test_a_bad_placeholder_is_refused_before_any_work(tmp_path, monkeypatch, bad):
    monkeypatch.chdir(tmp_path)
    with pytest.raises(SystemExit, match=r'cannot expand --bindir or --output .* only \{board\} is expanded'):
        merge_image.main([*ARGS, *bad])
    assert list(tmp_path.iterdir()) == []


def test_one_output_for_several_boards_is_refused(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    args = [
        '--board',
        'OpenShock-Core-V2 OpenShock-Core-V1',
        '--staticfs',
        'staticfs.bin',
        '--bindir',
        'boards/{board}',
        '--output',
        'merged.bin',
    ]
    with pytest.raises(SystemExit, match=r'same path for different boards'):
        merge_image.main(args)
    assert list(tmp_path.iterdir()) == []


def test_an_unwritable_output_dir_fails_that_board_and_continues(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    (tmp_path / 'merged').mkdir()
    (tmp_path / 'merged' / 'OpenShock-Core-V2').write_text('', encoding='utf-8')  # a file where the output dir must go
    merged = []
    monkeypatch.setattr(merge_image, 'merge_one', lambda board, *a: merged.append(board))
    args = [
        '--board',
        'OpenShock-Core-V2 OpenShock-Core-V1',
        '--staticfs',
        'staticfs.bin',
        '--bindir',
        'boards/{board}',
        '--output',
        'merged/{board}/merged.bin',
    ]
    with pytest.raises(SystemExit, match=r'failed to merge: OpenShock-Core-V2$'):
        merge_image.main(args)
    assert merged == ['OpenShock-Core-V1']


def test_merge_one_places_each_image_at_its_partition_offset(monkeypatch, tmp_path):
    import esptool

    calls = []
    monkeypatch.setattr(esptool, 'main', calls.append)
    merge_image.merge_one('OpenShock-Core-V2', tmp_path / 'bin', 'staticfs.bin', 'out.bin')

    argv = calls[0]
    assert argv[:2] == ['--chip', 'esp32s3']
    assert argv[argv.index('--flash-size') + 1] == 'keep'
    pairs = dict(zip(argv[7::2], argv[8::2], strict=True))
    assert pairs == {
        '0x0': str(tmp_path / 'bin' / 'bootloader.bin'),
        '0x8000': str(tmp_path / 'bin' / 'partition-table.bin'),
        '0x10000': str(tmp_path / 'bin' / 'app.bin'),
        '0x353000': 'staticfs.bin',
    }


def test_merge_one_refuses_a_table_without_static0(monkeypatch, tmp_path):
    monkeypatch.setattr(merge_image.partitions, 'read_partitions', lambda path: {'app0': {}})
    with pytest.raises(SystemExit, match='no static0 partition'):
        merge_image.merge_one('OpenShock-Core-V2', tmp_path, 'staticfs.bin', 'out.bin')
