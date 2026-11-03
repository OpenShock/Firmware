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
    magic, version, count, dir_len, image_len, image_hash, reserved = struct.unpack_from(
        wwwimage.HEADER_FORMAT, image, 0
    )
    assert (magic, version, count, reserved) == (wwwimage.MAGIC, 1, NODE_COUNT, b'\0' * 16)
    assert image[:4] == b'OSWW'
    assert dir_len % 4 == 0
    assert image_len == len(image) and image_len % 4 == 0
    # The hash is a full SHA-256 over the whole image, its own field zeroed
    assert len(image_hash) == 32
    assert image_hash == hashlib.sha256(image[:16] + b'\0' * 32 + image[48:]).digest()

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


def corrupt(image: bytes, offset: int, value: bytes, rehash: bool = True) -> bytes:
    """`image` with `value` written at `offset`, re-signed by default so the field check (not the hash) must catch it."""
    data = bytearray(image)
    data[offset : offset + len(value)] = value
    if rehash:
        image_len = struct.unpack_from('<I', data, wwwimage.IMAGE_LEN_OFFSET)[0]
        if wwwimage.HEADER_LEN <= image_len <= len(data):
            data[16:48] = wwwimage.compute_hash(bytes(data[:image_len]))
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
        (12, struct.pack('<I', 2), 'image length out of range'),  # unaligned
        (12, struct.pack('<I', 32), 'image length out of range'),  # shorter than the header
        (12, struct.pack('<I', 0x100000), 'image length out of range'),  # past the end
        (48, b'\1', 'reserved header field not zero'),
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


@pytest.mark.parametrize(
    'offset, value',
    [
        (6, struct.pack('<H', 200)),
        (16, b'\0' * 32),
        (48, b'\1'),
        (node(2) + 4, struct.pack('<I', 0x10)),
        (-1, b'\1'),  # a body byte
    ],
)
def test_any_unsigned_change_is_a_hash_mismatch_before_anything_else_is_parsed(offset, value):
    image = wwwimage.pack(ASSETS)
    with pytest.raises(wwwimage.WwwImageError, match='image hash mismatch'):
        wwwimage.parse(corrupt(image, offset % len(image), value, rehash=False))


def test_an_uppercase_name_in_the_image_is_rejected():
    image = wwwimage.pack(ASSETS)
    image = corrupt(image, image.index(b'index.html\0'), b'I')  # re-signed
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
        wwwimage.parse(corrupt(bytes(image), 0, b''))  # re-signed


def test_a_hash_over_the_bodies_alone_is_refused():
    # What the first draft of the format hashed: everything after the header
    image = wwwimage.pack(ASSETS)
    image = corrupt(image, 16, hashlib.sha256(image[wwwimage.HEADER_LEN :]).digest(), rehash=False)
    with pytest.raises(wwwimage.WwwImageError, match='image hash mismatch'):
        wwwimage.parse(image)


def test_a_truncated_image_is_rejected():
    image = wwwimage.pack(ASSETS)
    for length in (0, 10, wwwimage.HEADER_LEN, len(image) - 1):
        with pytest.raises(wwwimage.WwwImageError):
            wwwimage.parse(image[:length])


def test_a_truncated_image_is_caught_by_its_length_before_hashing():
    image = wwwimage.pack(ASSETS)
    with pytest.raises(wwwimage.WwwImageError, match='image length out of range'):
        wwwimage.parse(image[:-1])


@pytest.mark.parametrize(
    'delta, error',
    [
        (-4, 'body out of range'),  # chunk.js (6 bytes, padded to 8) now runs past the image
        (4, 'does not match the bodies'),  # trailing bytes the signature covers but no node accounts for
    ],
)
def test_an_image_length_that_is_not_where_the_bodies_end_is_rejected(delta, error):
    image = wwwimage.pack(ASSETS) + b'\0' * 4
    image_len = len(image) - 4 + delta
    image = corrupt(image, wwwimage.IMAGE_LEN_OFFSET, struct.pack('<I', image_len))
    with pytest.raises(wwwimage.WwwImageError, match=error):
        wwwimage.parse(image)


def test_the_bytes_after_the_image_are_ignored():
    image = wwwimage.pack(ASSETS)
    parsed = wwwimage.parse(image + b'\xff' * 4096)
    assert parsed.size == len(image)
    assert parsed.find('index.html').data == ASSETS[0].data


@pytest.mark.parametrize(
    'path, immutable',
    [
        ('_app/immutable/chunk.abc123.js', True),
        ('_app/immutable/assets/app.css', True),
        ('_app/version.json', False),
        ('index.html', False),
        ('immutable/x.js', False),  # only SvelteKit's _app/immutable/ carries content hashes
        ('static/_app/immutable/x.js', False),
    ],
)
def test_only_svelte_kit_hashed_assets_are_immutable(path, immutable):
    assert wwwimage.is_immutable(path) is immutable


def test_main_packs_a_directory_as_is(tmp_path):
    source = tmp_path / 'assets'
    (source / '_app' / 'immutable').mkdir(parents=True)
    (source / 'index.html').write_bytes(b'<title>x</title>')
    (source / '_app' / 'immutable' / 'chunk.js').write_bytes(b'1')
    output = tmp_path / 'out.bin'

    assert wwwimage.main([str(source), str(output)]) == 0

    parsed = wwwimage.parse(output.read_bytes())
    assert [(e.path, e.data, e.immutable) for e in parsed.entries] == [
        ('_app/immutable/chunk.js', b'1', True),
        ('index.html', b'<title>x</title>', False),
    ]
