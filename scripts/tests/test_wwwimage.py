"""Tests for scripts/utils/wwwimage.py: the flat captive-portal asset image, packed here and parsed by the firmware."""

import hashlib
import struct

import pytest

from utils import wwwimage

ASSETS = [
    wwwimage.Entry('index.html', b'<html>' * 7, gzip=True),
    wwwimage.Entry('logo.svg', b'<svg/>', gzip=True),
    wwwimage.Entry('LICENSES.txt', b'MIT', gzip=True),
    wwwimage.Entry('_app/immutable/chunk.js', b'js' * 3, gzip=True, immutable=True),
]


def test_round_trip_keeps_every_entry_sorted_by_path():
    image = wwwimage.pack(ASSETS)
    parsed = wwwimage.parse(image)
    assert [e.path for e in parsed.entries] == ['LICENSES.txt', '_app/immutable/chunk.js', 'index.html', 'logo.svg']
    for asset in ASSETS:
        found = parsed.find(asset.path)
        assert found == asset
    assert parsed.size == len(image)
    assert parsed.find('missing') is None


def test_layout_is_what_the_firmware_header_describes():
    image = wwwimage.pack(ASSETS)
    magic, version, count, dir_len, body_hash, reserved = struct.unpack_from(wwwimage.HEADER_FORMAT, image, 0)
    assert (magic, version, count, reserved) == (wwwimage.MAGIC, 1, 4, b'\0' * 4)
    assert image[:4] == b'OSWW'
    assert dir_len % 4 == 0
    # The hash covers the whole image, its own field zeroed
    assert body_hash == hashlib.sha256(image[:12] + b'\0' * 16 + image[28:]).digest()[:16]
    assert len(image) % 4 == 0

    body_start = wwwimage.HEADER_LEN + dir_len
    previous_end = body_start
    for index in range(count):
        path_off, flags, offset, size, reserved = struct.unpack_from(
            wwwimage.ENTRY_FORMAT, image, wwwimage.HEADER_LEN + 16 * index
        )
        assert reserved == 0
        assert offset == previous_end and offset % 4 == 0
        previous_end = offset + size + (-size) % 4
    assert previous_end == len(image)
    strings = image[wwwimage.HEADER_LEN + 16 * count : body_start]
    assert strings.startswith(b'LICENSES.txt\0_app/immutable/chunk.js\0index.html\0logo.svg\0')


def test_packing_is_deterministic_and_order_independent():
    assert wwwimage.pack(ASSETS) == wwwimage.pack(list(reversed(ASSETS)))


def test_an_empty_image_is_valid():
    parsed = wwwimage.parse(wwwimage.pack([]))
    assert parsed.entries == ()
    assert parsed.size == wwwimage.HEADER_LEN


@pytest.mark.parametrize(
    'path',
    ['/index.html', 'index.html/', 'a//b', 'a\\b', '', '../x', 'a/./b', 'x' * 256],
)
def test_bad_paths_are_refused(path):
    with pytest.raises(wwwimage.WwwImageError, match='bad asset path'):
        wwwimage.pack([wwwimage.Entry(path, b'')])


def test_duplicate_paths_are_refused():
    with pytest.raises(wwwimage.WwwImageError, match='duplicate'):
        wwwimage.pack([wwwimage.Entry('a', b'1'), wwwimage.Entry('a', b'2')])


def corrupt(image: bytes, offset: int, value: bytes) -> bytes:
    data = bytearray(image)
    data[offset : offset + len(value)] = value
    return bytes(data)


@pytest.mark.parametrize(
    'offset, value, error',
    [
        (0, b'XXXX', 'bad magic'),
        (4, struct.pack('<H', 2), 'unsupported version'),
        (6, struct.pack('<H', 200), 'directory length out of range'),
        (8, struct.pack('<I', 2), 'directory length out of range'),
        (8, struct.pack('<I', 0x100000), 'directory length out of range'),
        (12, b'\0' * 16, 'image hash mismatch'),
        (28, b'\1\0\0\0', 'reserved header field not zero'),
        (wwwimage.HEADER_LEN + 12, struct.pack('<I', 1), 'reserved field not zero'),
        (wwwimage.HEADER_LEN + 0, struct.pack('<H', 0xFFFF), 'path offset out of range'),
        (wwwimage.HEADER_LEN + 2, struct.pack('<H', 0x8000), 'unknown flags'),
        (wwwimage.HEADER_LEN + 4, struct.pack('<I', 0x10), 'body out of range'),
        (wwwimage.HEADER_LEN + 8, struct.pack('<I', 0x7FFFFFFF), 'body out of range'),
    ],
)
def test_every_corrupted_field_is_rejected(offset, value, error):
    with pytest.raises(wwwimage.WwwImageError, match=error):
        wwwimage.parse(corrupt(wwwimage.pack(ASSETS), offset, value))


def test_entries_out_of_order_are_rejected():
    image = bytearray(wwwimage.pack(ASSETS))
    # Swap the first two entries' path offsets: the paths are then no longer increasing
    first = image[wwwimage.HEADER_LEN : wwwimage.HEADER_LEN + 2]
    second = image[wwwimage.HEADER_LEN + 16 : wwwimage.HEADER_LEN + 18]
    image[wwwimage.HEADER_LEN : wwwimage.HEADER_LEN + 2] = second
    image[wwwimage.HEADER_LEN + 16 : wwwimage.HEADER_LEN + 18] = first
    with pytest.raises(wwwimage.WwwImageError, match='not strictly increasing|image hash'):
        wwwimage.parse(bytes(image))


def test_a_hash_over_the_bodies_alone_is_refused():
    # What the first draft of the format hashed: everything after the header
    image = wwwimage.pack(ASSETS)
    image = corrupt(image, 12, hashlib.sha256(image[wwwimage.HEADER_LEN :]).digest()[:16])
    with pytest.raises(wwwimage.WwwImageError, match='image hash mismatch'):
        wwwimage.parse(image)


def test_a_truncated_image_is_rejected():
    image = wwwimage.pack(ASSETS)
    for length in (0, 10, wwwimage.HEADER_LEN, len(image) - 1):
        with pytest.raises(wwwimage.WwwImageError):
            wwwimage.parse(image[:length])


def test_trailing_bytes_after_the_image_are_ignored():
    # The partition is larger than the image; the hash covers the image only, so 0xFF after it does not matter.
    image = wwwimage.pack(ASSETS)
    parsed = wwwimage.parse(image + b'\xff' * 4096)
    assert parsed.size == len(image)
