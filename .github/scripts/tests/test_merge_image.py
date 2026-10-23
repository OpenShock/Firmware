"""Tests for the command line of scripts/merge_image.py; esptool itself is never run."""

import pytest

import merge_image

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
