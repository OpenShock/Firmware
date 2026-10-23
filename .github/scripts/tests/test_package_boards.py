"""Tests for scripts/package_boards.py: per-variant images in, stamped per-board images out."""

import package_boards
from support import make_image
from utils import boards, espimage, stamping


def write_variant(variants, board):
    """The build directory of this board's variant, holding an unstamped app."""
    vid = boards.variant_id(boards.board_fragment(board))
    src = variants / vid
    src.mkdir(parents=True)
    descriptor = espimage.Descriptor('', 'ESP32-S3', -1, -1, -1, -1, 0, 0).pack()
    (src / 'app.bin').write_bytes(make_image(descriptor, chip_id=espimage.CHIP_IDS['esp32s3']))
    (src / 'bootloader.bin').write_bytes(b'boot')
    (src / 'partition-table.bin').write_bytes(b'table')
    return vid


def test_packages_every_board_of_a_variant(tmp_path):
    variants, out = tmp_path / 'variants', tmp_path / 'out'
    vid = write_variant(variants, 'OpenShock-Core-V2')

    assert package_boards.package(['OpenShock-Core-V2', 'Wemos-Lolin-S3-Mini'], variants, out) == []

    for board in ('OpenShock-Core-V2', 'Wemos-Lolin-S3-Mini'):
        stamping.verify_image(espimage.load(out / board / 'app.bin'), boards.read_board(board), board)
        # The bootloader and partition table ship as the variant built them, whatever the board's flash size.
        assert (out / board / 'bootloader.bin').read_bytes() == b'boot'
        assert (out / board / 'partition-table.bin').read_bytes() == b'table'
    # The variant's own image stays unstamped, so it can serve the next board.
    assert stamping.read_descriptor(espimage.load(variants / vid / 'app.bin'), 'app').board == ''


def test_a_missing_variant_fails_only_its_boards(tmp_path, capsys):
    variants, out = tmp_path / 'variants', tmp_path / 'out'
    write_variant(variants, 'OpenShock-Core-V2')
    # Core-V1 is on the UART console, a different variant from Core-V2's.
    failed = package_boards.package(['OpenShock-Core-V1', 'OpenShock-Core-V2'], variants, out)
    assert failed == ['OpenShock-Core-V1']
    assert 'no build of variant' in capsys.readouterr().err
    assert (out / 'OpenShock-Core-V2' / 'app.bin').is_file()
