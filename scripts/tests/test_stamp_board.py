"""Tests for scripts/stamp_board.py and the board/image helpers under scripts/utils."""

import struct

import pytest
import stamp_board
from support import blank, make_image, write_fragment

from utils import boards, espimage, stamping

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
    with pytest.raises(SystemExit, match=r'Bad\.defaults:2: '):
        boards.read_board_fragment(write_fragment(tmp_path, 'Bad', f'CONFIG_IDF_TARGET="esp32"\n{line}\n'))


ETH_FRAGMENT = (
    'CONFIG_IDF_TARGET="esp32"\n'
    'OPENSHOCK_ETHERNET_LAN8720=y\n'
    'OPENSHOCK_ETH_PHY_ADDR=0\n'
    'OPENSHOCK_ETH_PHY_POWER_GPIO=12\n'
    'OPENSHOCK_ETH_MDC_GPIO=23\n'
    'OPENSHOCK_ETH_MDIO_GPIO=18\n'
)


def test_fragment_parses_an_ethernet_phy(tmp_path):
    spec = boards.read_board_fragment(write_fragment(tmp_path, 'Eth', ETH_FRAGMENT))
    assert spec.eth_phy == boards.ETH_PHY_LAN8720
    assert (spec.eth_phy_addr, spec.eth_power_gpio, spec.eth_mdc_gpio, spec.eth_mdio_gpio) == (0, 12, 23, 18)

    desc = stamping.descriptor_for(spec)
    assert (desc.eth_phy, desc.eth_phy_addr, desc.eth_power_gpio, desc.eth_mdc_gpio, desc.eth_mdio_gpio) == (
        1,
        0,
        12,
        23,
        18,
    )
    assert espimage.unpack_descriptor(desc.pack()) == desc


def test_a_board_without_ethernet_stamps_zeroed_ethernet_fields(tmp_path):
    # Zero is what every descriptor carried before the Ethernet fields existed, and what it must keep meaning.
    spec = boards.read_board_fragment(write_fragment(tmp_path, 'Bare', 'CONFIG_IDF_TARGET="esp32"\n'))
    assert spec.eth_phy == boards.ETH_PHY_NONE
    packed = stamping.descriptor_for(spec).pack()
    assert packed[88 : espimage.DESC_CRC_LEN] == bytes(espimage.DESC_CRC_LEN - 88)


@pytest.mark.parametrize(
    ('body', 'pattern'),
    [
        ('OPENSHOCK_ETH_MDC_GPIO=23\n', 'OPENSHOCK_ETH_MDC_GPIO set without OPENSHOCK_ETHERNET_LAN8720=y'),
        ('OPENSHOCK_ETHERNET_LAN8720=y\nOPENSHOCK_ETH_MDC_GPIO=23\n', 'needs OPENSHOCK_ETH_MDIO_GPIO'),
        ('OPENSHOCK_ETHERNET_LAN8720=y\nOPENSHOCK_ETH_PHY_ADDR=32\n', r'must be -1 \(auto\) or 0-31'),
    ],
)
def test_fragment_rejects_bad_ethernet_settings(tmp_path, body, pattern):
    with pytest.raises(SystemExit, match=pattern):
        boards.read_board_fragment(write_fragment(tmp_path, 'Bad', f'CONFIG_IDF_TARGET="esp32"\n{body}'))


def test_fragment_rejects_an_unknown_target(tmp_path):
    with pytest.raises(SystemExit, match="unknown IDF target 'esp32p4'"):
        boards.read_board_fragment(write_fragment(tmp_path, 'P4', 'CONFIG_IDF_TARGET="esp32p4"\n'))


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
    with pytest.raises(espimage.ImageError, match='does not lie inside a single segment'):
        image.patch(image.segments[0].offset + image.segments[0].length - 4, b'12345678')


def _set(raw: bytes, offset: int, value: int) -> bytes:
    data = bytearray(raw)
    data[offset] = value
    return bytes(data)


def _first_segment_end(raw: bytes) -> int:
    (length,) = struct.unpack_from('<I', raw, espimage.HEADER_LEN + 4)
    return espimage.HEADER_LEN + espimage.SEGMENT_HEADER_LEN + length


@pytest.mark.parametrize(
    'mutate, error',
    [
        (lambda raw: raw[:20], 'image too small'),
        (lambda raw: _set(raw, 0, 0xE8), 'bad image magic 0xE8'),
        (lambda raw: _set(raw, 1, 0), 'unreasonable segment count 0'),
        (lambda raw: _set(raw, 1, espimage.MAX_SEGMENTS + 1), 'unreasonable segment count 17'),
        # Cut right after the first segment's data, so the second segment's header is missing.
        (lambda raw: raw[: _first_segment_end(raw)], 'segment 1: header runs past the end'),
        (lambda raw: raw + b'\0', 'expected .* from its segment table'),
        (lambda raw: raw[:-1], 'expected .* from its segment table'),
    ],
)
def test_parse_rejects_a_malformed_image(mutate, error):
    with pytest.raises(espimage.ImageError, match=error):
        espimage.parse(mutate(make_image(blank())))


def test_parse_rejects_a_segment_running_past_the_end():
    raw = bytearray(make_image(blank()))
    struct.pack_into('<I', raw, espimage.HEADER_LEN + 4, len(raw))  # first segment's length
    with pytest.raises(espimage.ImageError, match='segment 0: data runs past the end'):
        espimage.parse(bytes(raw))


@pytest.mark.parametrize(
    'offset, value, error',
    [
        (0, 0x00, 'bad magic'),
        (7, 0x00, 'bad length 0'),  # the u16 length is 0x0100, little-endian
        (4, 0x02, 'unsupported schema version 2'),
    ],
)
def test_unpack_descriptor_rejects_a_foreign_block(offset, value, error):
    data = bytearray(blank())
    data[offset] = value
    with pytest.raises(espimage.DescriptorError, match=error):
        espimage.unpack_descriptor(bytes(data))


def test_unpack_descriptor_needs_a_whole_descriptor():
    with pytest.raises(espimage.DescriptorError, match='need 256 bytes, got 10'):
        espimage.unpack_descriptor(b'\0' * 10)


@pytest.fixture
def test_board(tmp_path, monkeypatch):
    """A board named Test-S3, as boards/Test-S3.defaults would describe it."""
    fragment = write_fragment(tmp_path, 'Test-S3', S3_FRAGMENT)
    monkeypatch.setattr(boards, 'board_fragment', lambda board: fragment)
    return 'Test-S3'


def cli(*argv):
    return stamp_board.main([str(a) for a in argv])


def test_stamp_verify_and_read(tmp_path, capsys, test_board):
    image = tmp_path / 'firmware.bin'
    image.write_bytes(make_image(blank()))

    # A fresh build's placeholder reads back as unstamped, and does not verify as the board.
    assert cli('read', '--image', image) == 0
    assert '"stamped": false' in capsys.readouterr().out
    with pytest.raises(SystemExit, match='does not match Test-S3'):
        cli('verify', '--image', image, '--board', test_board)

    assert cli('stamp', '--image', image, '--board', test_board) == 0
    stamped = image.read_bytes()
    assert cli('verify', '--image', image, '--board', test_board) == 0
    capsys.readouterr()
    assert cli('read', '--image', image) == 0
    assert '"board": "Test-S3"' in capsys.readouterr().out

    # Stamping again is a byte-identical no-op, and leaves no temporary file behind.
    assert cli('stamp', '--image', image, '--board', test_board) == 0
    assert image.read_bytes() == stamped
    assert sorted(p.name for p in tmp_path.iterdir()) == sorted(['firmware.bin', 'Test-S3.defaults'])


def test_a_refused_stamp_leaves_the_image_untouched(tmp_path, test_board):
    image = tmp_path / 'esp32.bin'
    original = make_image(blank('ESP32'), chip_id=0x0000)
    image.write_bytes(original)
    with pytest.raises(SystemExit):
        cli('stamp', '--image', image, '--board', test_board)
    assert image.read_bytes() == original


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


def test_descriptor_format_fills_the_descriptor():
    assert struct.calcsize(espimage.DESC_FORMAT) == espimage.DESC_LEN
