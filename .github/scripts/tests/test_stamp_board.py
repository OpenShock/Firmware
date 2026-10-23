"""Tests for scripts/stamp_board.py and the board/image helpers under scripts/utils."""

import pytest

import stamp_board
from support import make_image, write_fragment
from utils import boards, espimage


S3_FRAGMENT = (
    'CONFIG_IDF_TARGET="esp32s3"\n'
    'CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\n'
    'OPENSHOCK_LED_WS2812B=14\n'
    'OPENSHOCK_LED_GPIO=13\n'
    'OPENSHOCK_RF_TX_GPIO=1\n'
    'OPENSHOCK_ESTOP_PIN=38\n'
    'OPENSHOCK_LED_SWAP_RG_CHANNELS=y\n'
    'OPENSHOCK_GPIO_BYPASS=15, 2\n'
)


def test_fragment_parses_into_a_spec(tmp_path):
    spec = boards.read_board_fragment(write_fragment(tmp_path, 'Test-S3', S3_FRAGMENT))
    assert spec.name == 'Test-S3'
    assert spec.target == 'esp32s3'
    assert spec.chip == 'ESP32-S3'
    assert (spec.rf_tx_gpio, spec.estop_gpio, spec.led_gpio, spec.ws2812b_gpio) == (1, 38, 13, 14)
    assert spec.led_swap_rg is True
    assert spec.gpio_bypass == (2, 15)
    assert spec.gpio_bypass_mask == (1 << 2) | (1 << 15)
    assert spec.flags == boards.FLAG_LED_SWAP_RG


def test_fragment_defaults_to_no_pins(tmp_path):
    spec = boards.read_board_fragment(write_fragment(tmp_path, 'Bare', 'CONFIG_IDF_TARGET="esp32c3"\n'))
    assert spec.chip == 'ESP32-C3'
    assert (spec.rf_tx_gpio, spec.estop_gpio, spec.led_gpio, spec.ws2812b_gpio) == (-1, -1, -1, -1)
    assert spec.gpio_bypass == ()
    assert spec.flags == 0


@pytest.mark.parametrize(
    'line',
    [
        'OPENSHOCK_LED_GPI0=2',  # typo in the key
        'OPENSHOCK_LED_GPIO=two',
        'OPENSHOCK_LED_SWAP_RG_CHANNELS=1',
        'OPENSHOCK_GPIO_BYPASS=2,2',
        'OPENSHOCK_GPIO_BYPASS=-1',
        'OPENSHOCK_GPIO_BYPASS=64',
        'garbage',
    ],
)
def test_fragment_rejects_bad_lines(tmp_path, line):
    with pytest.raises(SystemExit):
        boards.read_board_fragment(write_fragment(tmp_path, 'Bad', f'CONFIG_IDF_TARGET="esp32"\n{line}\n'))


@pytest.mark.parametrize('name', ['L' * (boards.NAME_MAX_LEN + 1), 'Bo\u00e4rd'])
def test_fragment_rejects_a_name_the_descriptor_cannot_hold(tmp_path, name):
    with pytest.raises(SystemExit, match=f'at most {boards.NAME_MAX_LEN} ASCII characters'):
        boards.read_board_fragment(write_fragment(tmp_path, name, 'CONFIG_IDF_TARGET="esp32"\n'))


def test_descriptor_round_trips():
    desc = espimage.Descriptor('OpenShock-Core-V2', 'ESP32-S3', 1, 38, 13, 14, boards.FLAG_LED_SWAP_RG, 0x8004)
    packed = desc.pack()
    assert len(packed) == espimage.DESC_LEN
    assert packed[:4] == b'OSBD'
    assert espimage.unpack_descriptor(packed) == desc


def test_descriptor_crc_covers_every_field():
    packed = bytearray(espimage.Descriptor('A', 'ESP32', 4, -1, -1, -1, 0, 0).pack())
    packed[8] ^= 0x01  # first byte of the board name
    with pytest.raises(espimage.DescriptorError, match='crc'):
        espimage.unpack_descriptor(bytes(packed))


def test_descriptor_rejects_overlong_names():
    with pytest.raises(ValueError):
        espimage.Descriptor('x' * 48, 'ESP32', -1, -1, -1, -1, 0, 0).pack()
    with pytest.raises(ValueError):
        espimage.Descriptor('A', '', -1, -1, -1, -1, 0, 0).pack()


def test_image_parse_and_verify_round_trip():
    raw = make_image(espimage.Descriptor('Blank', 'ESP32-S3', -1, -1, -1, -1, 0, 0).pack())
    image = espimage.parse(raw)
    image.verify()
    assert image.chip_id == 0x0009
    assert image.hash_appended
    assert len(image.segments) == 2
    assert espimage.unpack_descriptor(image.descriptor_bytes()).board == 'Blank'


def test_image_verify_catches_a_flipped_byte():
    raw = bytearray(make_image(None))
    raw[espimage.DESC_OFFSET + 3] ^= 0xFF
    with pytest.raises(espimage.ImageError, match='checksum'):
        espimage.parse(bytes(raw)).verify()


def test_patch_recomputes_checksum_and_digest():
    original = espimage.parse(make_image(espimage.Descriptor('Blank', 'ESP32-S3', -1, -1, -1, -1, 0, 0).pack()))
    stamped = original.patch(espimage.DESC_OFFSET, espimage.Descriptor('Real', 'ESP32-S3', 1, 2, 3, 4, 1, 0).pack())
    stamped.verify()
    assert len(stamped.data) == len(original.data)
    assert espimage.unpack_descriptor(stamped.descriptor_bytes()).board == 'Real'
    # Everything outside the descriptor, the checksum byte and the digest is untouched.
    for offset, byte in enumerate(original.data):
        if espimage.DESC_OFFSET <= offset < espimage.DESC_OFFSET + espimage.DESC_LEN:
            continue
        if offset == original.checksum_offset or offset >= original.digest_offset:
            continue
        assert stamped.data[offset] == byte, offset


def test_patch_without_appended_hash():
    original = espimage.parse(
        make_image(espimage.Descriptor('Blank', 'ESP32', -1, -1, -1, -1, 0, 0).pack(), chip_id=0, hash_appended=False)
    )
    stamped = original.patch(espimage.DESC_OFFSET, espimage.Descriptor('Real', 'ESP32', 4, -1, -1, -1, 0, 0).pack())
    stamped.verify()
    assert stamped.digest_offset is None


def test_patch_refuses_to_cross_a_segment_boundary():
    image = espimage.parse(make_image(None))
    with pytest.raises(espimage.ImageError):
        image.patch(image.segments[0].offset + image.segments[0].length - 4, b'12345678')


@pytest.fixture
def test_board(tmp_path, monkeypatch):
    """A board named Test-S3, as boards/Test-S3.defaults would describe it."""
    fragment = write_fragment(tmp_path, 'Test-S3', S3_FRAGMENT)
    monkeypatch.setattr(boards, 'board_fragment', lambda board: fragment)
    return 'Test-S3'


def cli(*argv):
    return stamp_board.main([str(a) for a in argv])


def blank(chip='ESP32-S3'):
    return espimage.Descriptor('', chip, -1, -1, -1, -1, 0, 0).pack()


def test_stamp_verify_and_read(tmp_path, capsys, test_board):
    image = tmp_path / 'firmware.bin'
    image.write_bytes(make_image(blank()))

    # A fresh build's placeholder reads back as unstamped, and does not verify as the board.
    assert cli('read', '--image', image) == 0
    assert '"stamped": false' in capsys.readouterr().out
    with pytest.raises(SystemExit):
        cli('verify', '--image', image, '--board', test_board)

    assert cli('stamp', '--image', image, '--board', test_board) == 0
    stamped = image.read_bytes()
    assert cli('verify', '--image', image, '--board', test_board) == 0
    capsys.readouterr()
    assert cli('read', '--image', image) == 0
    assert '"board": "Test-S3"' in capsys.readouterr().out

    # Stamping again is a byte-identical no-op.
    assert cli('stamp', '--image', image, '--board', test_board) == 0
    assert image.read_bytes() == stamped


def test_stamp_refuses_an_image_without_a_descriptor(tmp_path, test_board):
    image = tmp_path / 'legacy.bin'
    image.write_bytes(make_image(None))
    with pytest.raises(SystemExit, match='no board descriptor'):
        cli('stamp', '--image', image, '--board', test_board)


def test_stamp_refuses_the_wrong_chip(tmp_path, test_board):
    image = tmp_path / 'esp32.bin'
    image.write_bytes(make_image(blank('ESP32'), chip_id=0x0000))
    with pytest.raises(SystemExit, match='chip id'):
        cli('stamp', '--image', image, '--board', test_board)
