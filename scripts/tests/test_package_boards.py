"""Tests for scripts/package_boards.py: per-variant images in, stamped per-board images out."""

import shutil

import package_boards
import pytest
from support import blank, make_image

from utils import boards, espimage, stamping


@pytest.fixture
def dirs(tmp_path):
    return tmp_path / 'variants', tmp_path / 'out'


def write_variant(variants, board):
    """The build directory of this board's variant, holding an unstamped app."""
    vid = boards.variant_id(boards.board_fragment(board))
    src = variants / vid
    src.mkdir(parents=True)
    (src / 'app.bin').write_bytes(make_image(blank(), chip_id=espimage.CHIP_IDS['esp32s3']))
    (src / 'bootloader.bin').write_bytes(b'boot')
    (src / 'partition-table.bin').write_bytes(b'table')
    return vid


def test_packages_every_board_of_a_variant(dirs):
    variants, out = dirs
    vid = write_variant(variants, 'OpenShock-Core-V2')

    assert package_boards.package(['OpenShock-Core-V2', 'Wemos-Lolin-S3-Mini'], variants, out) == []

    for board in ('OpenShock-Core-V2', 'Wemos-Lolin-S3-Mini'):
        stamping.verify_image(espimage.load(out / board / 'app.bin'), boards.read_board(board), board)
        # The bootloader and partition table ship as the variant built them, whatever the board's flash size.
        assert (out / board / 'bootloader.bin').read_bytes() == b'boot'
        assert (out / board / 'partition-table.bin').read_bytes() == b'table'
    # The variant's own image stays unstamped, so it can serve the next board.
    assert stamping.read_descriptor(espimage.load(variants / vid / 'app.bin'), 'app').board == ''


def test_a_missing_variant_fails_only_its_boards(dirs, capsys):
    variants, out = dirs
    write_variant(variants, 'OpenShock-Core-V2')
    # Core-V1 is on the UART console, a different variant from Core-V2's.
    failed = package_boards.package(['OpenShock-Core-V1', 'OpenShock-Core-V2'], variants, out)
    assert failed == ['OpenShock-Core-V1']
    assert 'no build of variant' in capsys.readouterr().err
    assert (out / 'OpenShock-Core-V2' / 'app.bin').is_file()


def test_a_variant_missing_an_image_fails_only_its_boards(dirs, capsys):
    variants, out = dirs
    (variants / write_variant(variants, 'OpenShock-Core-V2') / 'bootloader.bin').unlink()
    write_variant(variants, 'OpenShock-Core-V1')
    failed = package_boards.package(['OpenShock-Core-V2', 'OpenShock-Core-V1'], variants, out)
    assert failed == ['OpenShock-Core-V2']
    assert 'FileNotFoundError' in capsys.readouterr().err
    assert (out / 'OpenShock-Core-V1' / 'app.bin').is_file()


def test_a_corrupt_stamp_is_caught_before_it_ships(dirs, capsys, monkeypatch):
    """The written image is verified, so a patch that broke the image fails its board instead of shipping."""
    variants, out = dirs
    write_variant(variants, 'OpenShock-Core-V2')
    real = stamping.stamp_image

    def corrupt_core_v2(image, spec, path):
        stamped = real(image, spec, path)
        if spec.name != 'OpenShock-Core-V2':
            return stamped
        data = bytearray(stamped.data)
        data[espimage.DESC_OFFSET + 8] ^= 0xFF  # inside the descriptor; the checksum no longer matches
        return espimage.parse(bytes(data))

    monkeypatch.setattr(package_boards.stamping, 'stamp_image', corrupt_core_v2)
    failed = package_boards.package(['OpenShock-Core-V2', 'Wemos-Lolin-S3-Mini'], variants, out)
    assert failed == ['OpenShock-Core-V2']
    assert 'checksum mismatch' in capsys.readouterr().err
    assert (out / 'Wemos-Lolin-S3-Mini' / 'app.bin').is_file()


def test_a_wrong_stamp_is_caught_before_it_ships(dirs, capsys, monkeypatch):
    """An image that carries another board's descriptor after stamping fails its board."""
    variants, out = dirs
    write_variant(variants, 'OpenShock-Core-V2')
    other = boards.read_board('Wemos-Lolin-S3-Mini')
    real = stamping.stamp_image
    monkeypatch.setattr(package_boards.stamping, 'stamp_image', lambda image, spec, path: real(image, other, path))
    failed = package_boards.package(['OpenShock-Core-V2', 'Wemos-Lolin-S3-Mini'], variants, out)
    assert failed == ['OpenShock-Core-V2']
    assert 'does not match OpenShock-Core-V2' in capsys.readouterr().err
    assert (out / 'Wemos-Lolin-S3-Mini' / 'app.bin').is_file()


def test_a_board_name_too_long_for_the_descriptor_fails_only_that_board(tmp_path, dirs, capsys, monkeypatch):
    root = tmp_path / 'repo'
    shutil.copytree(boards.ROOT / 'boards', root / 'boards')
    long_name = 'L' * (boards.NAME_MAX_LEN + 1)
    shutil.copyfile(root / 'boards' / 'OpenShock-Core-V2.defaults', root / 'boards' / f'{long_name}.defaults')
    monkeypatch.setattr(boards, 'ROOT', root)
    variants, out = dirs
    write_variant(variants, 'OpenShock-Core-V2')

    failed = package_boards.package([long_name, 'OpenShock-Core-V2'], variants, out)
    assert failed == [long_name]
    assert f'at most {boards.NAME_MAX_LEN} ASCII characters' in capsys.readouterr().err
    assert (out / 'OpenShock-Core-V2' / 'app.bin').is_file()
