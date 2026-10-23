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
    args = ['--board', 'OpenShock-Core-V2 OpenShock-Core-V1', '--staticfs', 'staticfs.bin', '--bindir', 'boards/{board}', '--output', 'merged.bin']
    with pytest.raises(SystemExit, match=r'same path for different boards'):
        merge_image.main(args)
    assert list(tmp_path.iterdir()) == []


def test_an_unwritable_output_dir_fails_that_board_and_continues(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    (tmp_path / 'merged').mkdir()
    (tmp_path / 'merged' / 'OpenShock-Core-V2').write_text('')  # a file where the output dir must go
    merged = []
    monkeypatch.setattr(merge_image, 'merge_one', lambda board, *a: merged.append(board))
    args = ['--board', 'OpenShock-Core-V2 OpenShock-Core-V1', '--staticfs', 'staticfs.bin', '--bindir', 'boards/{board}', '--output', 'merged/{board}/merged.bin']
    with pytest.raises(SystemExit, match=r'failed to merge: OpenShock-Core-V2$'):
        merge_image.main(args)
    assert merged == ['OpenShock-Core-V1']
