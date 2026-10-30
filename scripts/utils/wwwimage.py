"""The `www` image: the captive-portal assets as one read-only directory tree the firmware maps and serves.

Mirrors components/www/include/www/WwwImage.h; change both together and bump VERSION.

    header (64 B)
      magic      u32   'OSWW' as little-endian bytes
      version    u16   1
      node_count u16   number of nodes, the root and directories included
      dir_len    u32   bytes of nodes + string table (+ padding to 4)
      body_hash  u8[32]  sha256 of the whole image with this field zeroed
      reserved   u8[20]  zero
    node (16 B) x node_count
      name_off   u16   offset into the string table
      flags      u16   bit0 directory, bit1 immutable (files only; long cache)
      offset     u32   directory: index of its first child; file: body offset from the start of the image
      size       u32   directory: number of children; file: body bytes
      reserved   u32   zero
    string table: NUL-terminated names, one path component each (no '/'), no ASCII uppercase
    bodies: in node order, each padded to 4 bytes

Node 0 is the root directory (empty name). A directory's children are one contiguous run of nodes, strictly
sorted by name (byte order), and the runs are laid out breadth-first: walking the nodes in order, each directory's
run starts where the previous one ended, the first right after the root. Every body is gzipped; there is no flag
for it. Names are lowercase: the packer folds ASCII letters, and the firmware folds the request path the same way,
so URL case never matters.

Everything is 4-byte aligned. The output is a pure function of the entries: no timestamps, sorted order, so an
unchanged frontend gives a byte-identical image and the device can skip re-flashing it.
"""

import hashlib
import struct
from collections import deque
from dataclasses import dataclass

MAGIC = 0x5757534F  # 'OSWW'
VERSION = 1
HEADER_LEN = 64
NODE_LEN = 16
HEADER_FORMAT = '<IHHI32s20s'
NODE_FORMAT = '<HHIII'
BODY_HASH_LEN = 32
HEADER_RESERVED_LEN = 20
ALIGN = 4

FLAG_DIRECTORY = 1 << 0
FLAG_IMMUTABLE = 1 << 1

MAX_NAME_LEN = 255  # the string table is addressed by u16; this keeps a name comfortably below it


class WwwImageError(ValueError):
    pass


@dataclass(frozen=True)
class Entry:
    path: str
    data: bytes  # gzipped
    immutable: bool = False

    @property
    def flags(self) -> int:
        return FLAG_IMMUTABLE if self.immutable else 0


def _pad(length: int) -> int:
    return (-length) % ALIGN


def _check_path(path: str) -> list[bytes]:
    """The path's components, encoded. Raises WwwImageError for anything the firmware would not look up."""
    if not path or path.startswith('/') or '\\' in path or '//' in path or path.endswith('/'):
        raise WwwImageError(f'bad asset path {path!r}: no leading or trailing slash, forward slashes only')
    parts = path.split('/')
    if any(part in ('.', '..') for part in parts):
        raise WwwImageError(f'bad asset path {path!r}: no . or .. components')
    # bytes.lower() folds ASCII only, which is exactly what the firmware does to the request path
    encoded = [part.encode('utf-8').lower() for part in parts]
    if any(len(part) > MAX_NAME_LEN or b'\0' in part for part in encoded):
        raise WwwImageError(f'bad asset path {path!r}: at most {MAX_NAME_LEN} bytes per name, no NUL')
    return encoded


class _Dir:
    __slots__ = ('children',)

    def __init__(self) -> None:
        self.children: dict[bytes, _Dir | Entry] = {}


def _build_tree(entries: list[Entry]) -> _Dir:
    root = _Dir()
    for entry in entries:
        *dirs, leaf = _check_path(entry.path)
        node = root
        for name in dirs:
            child = node.children.get(name)
            if child is None:
                child = _Dir()
                node.children[name] = child
            elif not isinstance(child, _Dir):
                raise WwwImageError(f'{entry.path!r}: {name.decode()!r} is both a file and a directory')
            node = child
        existing = node.children.get(leaf)
        if isinstance(existing, _Dir):
            raise WwwImageError(f'{entry.path!r}: {leaf.decode()!r} is both a file and a directory')
        if existing is not None:
            raise WwwImageError(f'duplicate asset path {entry.path!r}')
        node.children[leaf] = entry
    return root


def pack(entries: list[Entry]) -> bytes:
    """The image bytes for these entries. Raises WwwImageError on a bad, duplicate or conflicting path."""
    root = _build_tree(entries)

    # Breadth-first layout: node 0 is the root; each directory's children are one sorted run, allocated in node order.
    # A node is [name, flags, offset, size, data]; data is None for a directory.
    nodes: list[list] = [[b'', FLAG_DIRECTORY, 0, 0, None]]
    queue: deque[tuple[_Dir, int]] = deque([(root, 0)])
    while queue:
        directory, index = queue.popleft()
        names = sorted(directory.children)
        nodes[index][2] = len(nodes)
        nodes[index][3] = len(names)
        for name in names:
            child = directory.children[name]
            if isinstance(child, _Dir):
                nodes.append([name, FLAG_DIRECTORY, 0, 0, None])
                queue.append((child, len(nodes) - 1))
            else:
                nodes.append([name, child.flags, 0, len(child.data), child.data])
    if len(nodes) > 0xFFFF:
        raise WwwImageError('too many nodes')

    strings = bytearray()
    name_offsets: dict[bytes, int] = {}
    for name, *_ in nodes:
        if name not in name_offsets:
            name_offsets[name] = len(strings)
            strings += name + b'\0'
    if len(strings) > 0xFFFF:
        raise WwwImageError('string table too large')

    dir_len = NODE_LEN * len(nodes) + len(strings)
    dir_len += _pad(dir_len)

    body_offset = HEADER_LEN + dir_len
    table = bytearray()
    bodies = bytearray()
    for name, flags, offset, size, data in nodes:
        if data is not None:
            offset = body_offset + len(bodies)
            bodies += data
            bodies += b'\0' * _pad(len(data))
        table += struct.pack(NODE_FORMAT, name_offsets[name], flags, offset, size, 0)

    dir_bytes = bytes(table) + bytes(strings)
    dir_bytes += b'\0' * (dir_len - len(dir_bytes))
    after_header = dir_bytes + bytes(bodies)
    reserved = b'\0' * HEADER_RESERVED_LEN
    header = struct.pack(HEADER_FORMAT, MAGIC, VERSION, len(nodes), dir_len, b'\0' * BODY_HASH_LEN, reserved)
    body_hash = hashlib.sha256(header + after_header).digest()
    header = struct.pack(HEADER_FORMAT, MAGIC, VERSION, len(nodes), dir_len, body_hash, reserved)
    return header + after_header


@dataclass(frozen=True)
class Image:
    entries: tuple[Entry, ...]  # every file, with its full path, depth-first
    body_hash: bytes
    node_count: int
    size: int  # bytes of the image, header to the end of the last body's padding

    def find(self, path: str) -> Entry | None:
        """Case-insensitive for ASCII, as on the device."""
        folded = path.encode('utf-8').lower()
        return next((e for e in self.entries if e.path.encode('utf-8') == folded), None)


def parse(data: bytes) -> Image:
    """Decode and fully validate an image (what the firmware checks at mount). Raises WwwImageError."""
    if len(data) < HEADER_LEN:
        raise WwwImageError('too short for a header')
    magic, version, count, dir_len, body_hash, reserved = struct.unpack_from(HEADER_FORMAT, data, 0)
    if magic != MAGIC:
        raise WwwImageError(f'bad magic 0x{magic:08X}')
    if version != VERSION:
        raise WwwImageError(f'unsupported version {version}')
    if dir_len % ALIGN or dir_len < NODE_LEN * count or HEADER_LEN + dir_len > len(data):
        raise WwwImageError('directory length out of range')
    if reserved != b'\0' * HEADER_RESERVED_LEN:
        raise WwwImageError('reserved header field not zero')
    if count == 0:
        raise WwwImageError('no root node')
    strings_start = HEADER_LEN + NODE_LEN * count
    strings = data[strings_start : HEADER_LEN + dir_len]
    body_start = HEADER_LEN + dir_len

    nodes = [struct.unpack_from(NODE_FORMAT, data, HEADER_LEN + NODE_LEN * index) for index in range(count)]
    names: list[bytes] = []
    next_run = 1
    end = body_start
    for index, (name_off, flags, offset, size, node_reserved) in enumerate(nodes):
        if name_off >= len(strings):
            raise WwwImageError(f'node {index}: name offset out of range')
        nul = strings.find(b'\0', name_off)
        if nul < 0:
            raise WwwImageError(f'node {index}: unterminated name')
        name = strings[name_off:nul]
        if index == 0:
            if name or not flags & FLAG_DIRECTORY:
                raise WwwImageError('node 0 is not the root directory')
        elif not name or b'/' in name or name in (b'.', b'..') or name != name.lower():
            raise WwwImageError(f'node {index}: bad name {name!r}')
        if flags & ~(FLAG_DIRECTORY | FLAG_IMMUTABLE):
            raise WwwImageError(f'node {index}: unknown flags 0x{flags:04X}')
        if node_reserved:
            raise WwwImageError(f'node {index}: reserved field not zero')
        if flags & FLAG_DIRECTORY:
            if flags & FLAG_IMMUTABLE:
                raise WwwImageError(f'node {index}: immutable directory')
            if offset != next_run or size > count - next_run:
                raise WwwImageError(f'node {index}: bad child run')
            next_run += size
        else:
            if offset < body_start or offset % ALIGN or offset + size > len(data) or offset + size < offset:
                raise WwwImageError(f'node {index}: body out of range')
            end = max(end, offset + size + _pad(size))
        names.append(name)
    if next_run != count:
        raise WwwImageError('unreachable nodes')
    for index, (_, flags, offset, size, _) in enumerate(nodes):
        if flags & FLAG_DIRECTORY:
            run = names[offset : offset + size]
            if any(a >= b for a, b in zip(run, run[1:], strict=False)):
                raise WwwImageError(f'node {index}: children not strictly increasing')

    hashed = data[:12] + b'\0' * BODY_HASH_LEN + data[12 + BODY_HASH_LEN : end]
    if hashlib.sha256(hashed).digest() != body_hash:
        raise WwwImageError('image hash mismatch')

    entries: list[Entry] = []

    def walk(index: int, prefix: bytes) -> None:
        _, _, first, child_count, _ = nodes[index]
        for child in range(first, first + child_count):
            _, flags, offset, size, _ = nodes[child]
            path = prefix + names[child]
            if flags & FLAG_DIRECTORY:
                walk(child, path + b'/')
            else:
                entries.append(Entry(path.decode('utf-8'), data[offset : offset + size], bool(flags & FLAG_IMMUTABLE)))

    walk(0, b'')
    return Image(tuple(entries), body_hash, count, end)


def main(argv: list[str] | None = None) -> int:
    """``wwwimage.py <dir> <out.bin>``: pack a directory as-is. Test fixtures only; the build uses gen_staticfs.py."""
    import argparse
    from pathlib import Path

    ap = argparse.ArgumentParser(description='Pack a directory into a www image without gzipping (test fixtures)')
    ap.add_argument('directory', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args(argv)

    entries = [
        Entry(
            p.relative_to(args.directory).as_posix(),
            p.read_bytes(),
            immutable='/immutable/' in f'/{p.relative_to(args.directory).as_posix()}',
        )
        for p in sorted(args.directory.rglob('*'))
        if p.is_file()
    ]
    args.output.write_bytes(pack(entries))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
