"""Tests for scripts/stamp_board.py and the board/image helpers under scripts/utils."""

import hashlib
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'scripts'))

import stamp_board  # noqa: E402
from utils import boards, espimage  # noqa: E402


def make_image(
    descriptor: bytes | None, chip_id: int = 0x0009, hash_appended: bool = True, extra_segments: int = 1
) -> bytes:
    """A minimal but structurally valid app image: app descriptor, optional board descriptor, filler segments."""
    app_desc = struct.pack('<I', espimage.APP_DESC_MAGIC) + b'\0' * (espimage.APP_DESC_LEN - 4)
    rodata = app_desc + (descriptor if descriptor is not None else b'/dev/uart/0\0' * 24) + b'R' * 100
    segments = [(0x3C000020, rodata)] + [
        (0x40000000 + i * 0x1000, bytes([0x10 + i]) * (37 + i)) for i in range(extra_segments)
    ]

    header = bytearray(espimage.HEADER_LEN)
    header[0] = espimage.IMAGE_MAGIC
    header[1] = len(segments)
    struct.pack_into('<H', header, 0x0C, chip_id)
    header[0x17] = 1 if hash_appended else 0

    body = bytearray(header)
    for load_addr, data in segments:
        body += struct.pack('<II', load_addr, len(data)) + data
    checksum_offset = ((len(body) + 1 + 15) // 16) * 16 - 1
    body += b'\0' * (checksum_offset - len(body))
    checksum = espimage.CHECKSUM_INIT
    for _, data in segments:
        for byte in data:
            checksum ^= byte
    body.append(checksum)
    if hash_appended:
        body += hashlib.sha256(bytes(body)).digest()
    return bytes(body)


def write_fragment(tmp_path: Path, name: str, body: str) -> Path:
    fragment = tmp_path / f'{name}.defaults'
    fragment.write_text(body, encoding='utf-8')
    return fragment


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
    assert spec.pins == {k: -1 for k in boards.PIN_KEYS}
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


def test_every_repository_board_parses():
    for fragment in sorted((ROOT / 'boards').glob('*.defaults')):
        spec = boards.read_board_fragment(fragment)
        assert spec.chip in boards.TARGET_TO_CHIP.values(), fragment
        assert spec.target in espimage.CHIP_IDS, fragment


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


def test_placeholder_has_no_name_but_a_valid_crc():
    packed = espimage.Descriptor('', 'ESP32-S3', -1, -1, -1, -1, 0, 0).pack()
    assert espimage.unpack_descriptor(packed).board == ''


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


def test_stamp_verify_and_read(tmp_path, capsys):
    fragment = write_fragment(tmp_path, 'Test-S3', S3_FRAGMENT)
    image = tmp_path / 'firmware.bin'
    image.write_bytes(make_image(espimage.Descriptor('Blank', 'ESP32-S3', -1, -1, -1, -1, 0, 0).pack()))
    out = tmp_path / 'stamped.bin'

    assert (
        stamp_board.main(
            ['stamp', '--image', str(image), '--board', 'Test-S3', '--fragment', str(fragment), '--output', str(out)]
        )
        == 0
    )
    assert stamp_board.main(['verify', '--image', str(out), '--board', 'Test-S3', '--fragment', str(fragment)]) == 0
    assert stamp_board.main(['read', '--image', str(out)]) == 0
    printed = capsys.readouterr().out
    assert '"board": "Test-S3"' in printed
    assert '"stamped": true' in printed

    # A fresh build's placeholder (no board name) reads back as unstamped.
    placeholder = tmp_path / 'placeholder.bin'
    placeholder.write_bytes(make_image(espimage.Descriptor('', 'ESP32-S3', -1, -1, -1, -1, 0, 0).pack()))
    assert stamp_board.main(['read', '--image', str(placeholder)]) == 0
    assert '"stamped": false' in capsys.readouterr().out
    with pytest.raises(SystemExit):
        stamp_board.main(['verify', '--image', str(placeholder), '--board', 'Test-S3', '--fragment', str(fragment)])

    # The unstamped image does not verify as that board, and stamping is idempotent.
    with pytest.raises(SystemExit):
        stamp_board.main(['verify', '--image', str(image), '--board', 'Test-S3', '--fragment', str(fragment)])
    again = tmp_path / 'again.bin'
    assert (
        stamp_board.main(
            ['stamp', '--image', str(out), '--board', 'Test-S3', '--fragment', str(fragment), '--output', str(again)]
        )
        == 0
    )
    assert again.read_bytes() == out.read_bytes()


def test_stamp_refuses_an_image_without_a_descriptor(tmp_path):
    fragment = write_fragment(tmp_path, 'Test-S3', S3_FRAGMENT)
    image = tmp_path / 'legacy.bin'
    image.write_bytes(make_image(None))
    with pytest.raises(SystemExit, match='no board descriptor'):
        stamp_board.main(
            [
                'stamp',
                '--image',
                str(image),
                '--board',
                'Test-S3',
                '--fragment',
                str(fragment),
                '--output',
                str(tmp_path / 'x.bin'),
            ]
        )


def test_stamp_refuses_the_wrong_chip(tmp_path):
    fragment = write_fragment(tmp_path, 'Test-S3', S3_FRAGMENT)
    image = tmp_path / 'esp32.bin'
    image.write_bytes(make_image(espimage.Descriptor('Blank', 'ESP32', -1, -1, -1, -1, 0, 0).pack(), chip_id=0x0000))
    with pytest.raises(SystemExit, match='chip id'):
        stamp_board.main(
            [
                'stamp',
                '--image',
                str(image),
                '--board',
                'Test-S3',
                '--fragment',
                str(fragment),
                '--output',
                str(tmp_path / 'x.bin'),
            ]
        )
