"""Tests for scripts/utils/wwwimage.py: the captive-portal asset image, packed here and parsed by the firmware."""

import hashlib
import struct

import pytest

from utils import wwwimage

ASSETS = [
    wwwimage.Entry('index.html', b'<html>' * 7),
    wwwimage.Entry('logo.svg', b'<svg/>'),
    wwwimage.Entry('LICENSES.txt', b'MIT'),  # stored as licenses.txt
    wwwimage.Entry('_app/immutable/chunk.js', b'js' * 3, immutable=True),
]

# The node table for ASSETS, breadth-first, names folded to lowercase ('_' sorts before letters):
#   0 root (dir)          children [1, 5)
#   1 _app (dir)          children [5, 6)
#   2 index.html
#   3 licenses.txt
#   4 logo.svg
#   5 immutable (dir)     children [6, 7)
#   6 chunk.js
NODE_COUNT = 7


def node(index: int) -> int:
    return wwwimage.HEADER_LEN + wwwimage.NODE_LEN * index


def test_round_trip_keeps_every_entry():
    image = wwwimage.pack(ASSETS)
    parsed = wwwimage.parse(image)
    assert [e.path for e in parsed.entries] == ['_app/immutable/chunk.js', 'index.html', 'licenses.txt', 'logo.svg']
    for asset in ASSETS:
        found = parsed.find(asset.path)
        assert found is not None
        assert (found.path, found.data, found.immutable) == (asset.path.lower(), asset.data, asset.immutable)
    assert parsed.node_count == NODE_COUNT
    assert parsed.size == len(image)
    assert parsed.find('missing') is None
    assert parsed.find('_app') is None  # a directory is not an asset
    assert parsed.find('_app/immutable') is None


def test_layout_is_what_the_firmware_header_describes():
    image = wwwimage.pack(ASSETS)
    magic, version, count, dir_len, body_hash, reserved = struct.unpack_from(wwwimage.HEADER_FORMAT, image, 0)
    assert (magic, version, count, reserved) == (wwwimage.MAGIC, 1, NODE_COUNT, b'\0' * 20)
    assert image[:4] == b'OSWW'
    assert dir_len % 4 == 0
    # The hash is a full SHA-256 over the whole image, its own field zeroed
    assert len(body_hash) == 32
    assert body_hash == hashlib.sha256(image[:12] + b'\0' * 32 + image[44:]).digest()
    assert len(image) % 4 == 0

    nodes = [struct.unpack_from(wwwimage.NODE_FORMAT, image, node(i)) for i in range(count)]
    strings = image[node(count) : wwwimage.HEADER_LEN + dir_len]

    def name(off: int) -> bytes:
        return strings[off : strings.index(b'\0', off)]

    assert [name(n[0]) for n in nodes] == [
        b'',
        b'_app',
        b'index.html',
        b'licenses.txt',
        b'logo.svg',
        b'immutable',
        b'chunk.js',
    ]
    # Directories: flags, first child, child count
    assert nodes[0][1:4] == (wwwimage.FLAG_DIRECTORY, 1, 4)
    assert nodes[1][1:4] == (wwwimage.FLAG_DIRECTORY, 5, 1)
    assert nodes[5][1:4] == (wwwimage.FLAG_DIRECTORY, 6, 1)
    assert nodes[2][1] == 0
    assert nodes[6][1] == wwwimage.FLAG_IMMUTABLE

    # Bodies: in node order, right after the directory, each 4-byte aligned
    body_start = wwwimage.HEADER_LEN + dir_len
    previous_end = body_start
    for _, flags, offset, size, node_reserved in nodes:
        assert node_reserved == 0
        if flags & wwwimage.FLAG_DIRECTORY:
            continue
        assert offset == previous_end and offset % 4 == 0
        previous_end = offset + size + (-size) % 4
    assert previous_end == len(image)


def test_packing_is_deterministic_and_order_independent():
    assert wwwimage.pack(ASSETS) == wwwimage.pack(list(reversed(ASSETS)))


def test_an_empty_image_is_valid():
    image = wwwimage.pack([])
    parsed = wwwimage.parse(image)
    assert parsed.entries == ()
    assert parsed.node_count == 1
    assert parsed.size == len(image)


def test_names_are_shared_between_nodes():
    # Two files called the same thing in different directories store the name once
    image = wwwimage.pack([wwwimage.Entry('a/x.txt', b'1'), wwwimage.Entry('b/x.txt', b'2')])
    assert image.count(b'x.txt\0') == 1
    parsed = wwwimage.parse(image)
    assert parsed.find('a/x.txt').data == b'1'
    assert parsed.find('b/x.txt').data == b'2'


def test_names_are_folded_to_lowercase_and_looked_up_case_insensitively():
    image = wwwimage.pack([wwwimage.Entry('LICENSES.txt', b'MIT'), wwwimage.Entry('Dir/File.JS', b'1')])
    assert b'LICENSES' not in image and b'licenses.txt\0' in image
    parsed = wwwimage.parse(image)
    assert [e.path for e in parsed.entries] == ['dir/file.js', 'licenses.txt']
    assert parsed.find('LICENSES.TXT').data == b'MIT'
    assert parsed.find('dir/FILE.js').data == b'1'


@pytest.mark.parametrize(
    'entries, error',
    [
        ([wwwimage.Entry('a.txt', b'1'), wwwimage.Entry('A.TXT', b'2')], 'duplicate'),
        ([wwwimage.Entry('A', b'1'), wwwimage.Entry('a/b', b'2')], 'both a file and a directory'),
    ],
)
def test_paths_differing_only_by_case_collide(entries, error):
    with pytest.raises(wwwimage.WwwImageError, match=error):
        wwwimage.pack(entries)


@pytest.mark.parametrize(
    'path',
    ['/index.html', 'index.html/', 'a//b', 'a\\b', '', '../x', 'a/./b', 'x' * 256, 'a/' + 'x' * 256],
)
def test_bad_paths_are_refused(path):
    with pytest.raises(wwwimage.WwwImageError, match='bad asset path'):
        wwwimage.pack([wwwimage.Entry(path, b'')])


def test_duplicate_paths_are_refused():
    with pytest.raises(wwwimage.WwwImageError, match='duplicate'):
        wwwimage.pack([wwwimage.Entry('a', b'1'), wwwimage.Entry('a', b'2')])


@pytest.mark.parametrize('order', [(0, 1), (1, 0)])
def test_a_name_cannot_be_both_a_file_and_a_directory(order):
    entries = [wwwimage.Entry('a', b'1'), wwwimage.Entry('a/b', b'2')]
    with pytest.raises(wwwimage.WwwImageError, match='both a file and a directory'):
        wwwimage.pack([entries[i] for i in order])


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
        (6, struct.pack('<H', 0), 'no root node'),
        (8, struct.pack('<I', 2), 'directory length out of range'),
        (8, struct.pack('<I', 0x100000), 'directory length out of range'),
        (12, b'\0' * 32, 'image hash mismatch'),
        (44, b'\1\0\0\0', 'reserved header field not zero'),
        (63, b'\1', 'reserved header field not zero'),
        (node(0) + 2, struct.pack('<H', 0), 'not the root directory'),
        (node(0) + 4, struct.pack('<I', 2), 'bad child run'),  # the root's children start at 1
        (node(0) + 8, struct.pack('<I', 100), 'bad child run'),
        (node(1) + 2, struct.pack('<H', wwwimage.FLAG_DIRECTORY | wwwimage.FLAG_IMMUTABLE), 'immutable directory'),
        (node(1) + 4, struct.pack('<I', 6), 'bad child run'),  # _app's run must follow the root's
        (node(2) + 0, struct.pack('<H', 0xFFFF), 'name offset out of range'),
        (node(2) + 2, struct.pack('<H', 0x8000), 'unknown flags'),
        (node(2) + 4, struct.pack('<I', 0x10), 'body out of range'),
        (node(2) + 8, struct.pack('<I', 0x7FFFFFFF), 'body out of range'),
        (node(2) + 12, struct.pack('<I', 1), 'reserved field not zero'),
        (node(5) + 8, struct.pack('<I', 0), 'unreachable nodes'),  # chunk.js then belongs to no directory
    ],
)
def test_every_corrupted_field_is_rejected(offset, value, error):
    with pytest.raises(wwwimage.WwwImageError, match=error):
        wwwimage.parse(corrupt(wwwimage.pack(ASSETS), offset, value))


def test_an_uppercase_name_in_the_image_is_rejected():
    image = wwwimage.pack(ASSETS)
    image = corrupt(image, image.index(b'index.html\0'), b'I')
    with pytest.raises(wwwimage.WwwImageError, match='bad name'):
        wwwimage.parse(image)


def test_children_out_of_order_are_rejected():
    image = bytearray(wwwimage.pack(ASSETS))
    # Swap the names of index.html and licenses.txt: the root's children are then no longer increasing
    second = image[node(2) : node(2) + 2]
    third = image[node(3) : node(3) + 2]
    image[node(2) : node(2) + 2] = third
    image[node(3) : node(3) + 2] = second
    with pytest.raises(wwwimage.WwwImageError, match='not strictly increasing'):
        wwwimage.parse(bytes(image))


def test_a_hash_over_the_bodies_alone_is_refused():
    # What the first draft of the format hashed: everything after the header
    image = wwwimage.pack(ASSETS)
    image = corrupt(image, 12, hashlib.sha256(image[wwwimage.HEADER_LEN :]).digest())
    with pytest.raises(wwwimage.WwwImageError, match='image hash mismatch'):
        wwwimage.parse(image)


def test_a_truncated_image_is_rejected():
    image = wwwimage.pack(ASSETS)
    for length in (0, 10, wwwimage.HEADER_LEN, len(image) - 1):
        with pytest.raises(wwwimage.WwwImageError):
            wwwimage.parse(image[:length])
