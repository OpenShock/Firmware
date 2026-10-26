"""The `www` image: the captive-portal assets as one flat, read-only directory the firmware maps and serves.

Mirrors components/www/include/www/WwwImage.h; change both together and bump VERSION.

    header (32 B)
      magic      u32   'OSWW' as little-endian bytes
      version    u16   1
      count      u16   number of entries
      dir_len    u32   bytes of entries + string table (+ padding to 4)
      body_hash  u8[16]  sha256 of everything after the header, truncated
      reserved   u8[4]  zero
    entry (16 B) x count, sorted by path (byte order)
      path_off   u16   offset into the string table
      flags      u16   bit0 gzip, bit1 immutable (long cache)
      offset     u32   body offset from the start of the image
      size       u32   bytes
      reserved   u32   zero
    string table: NUL-terminated paths, no leading slash, forward slashes
    bodies: in entry order, each padded to 4 bytes

Everything is 4-byte aligned. The output is a pure function of the entries: no timestamps, sorted order, so an
unchanged frontend gives a byte-identical image and the device can skip re-flashing it.
"""

import hashlib
import struct
from dataclasses import dataclass

MAGIC = 0x5757534F  # 'OSWW'
VERSION = 1
HEADER_LEN = 32
ENTRY_LEN = 16
HEADER_FORMAT = '<IHHI16s4s'
ENTRY_FORMAT = '<HHIII'
BODY_HASH_LEN = 16
ALIGN = 4

FLAG_GZIP = 1 << 0
FLAG_IMMUTABLE = 1 << 1

MAX_PATH_LEN = 255  # the string table is addressed by u16; this keeps a path comfortably below it


class WwwImageError(ValueError):
    pass


@dataclass(frozen=True)
class Entry:
    path: str
    data: bytes
    gzip: bool = False
    immutable: bool = False

    @property
    def flags(self) -> int:
        return (FLAG_GZIP if self.gzip else 0) | (FLAG_IMMUTABLE if self.immutable else 0)


def _pad(length: int) -> int:
    return (-length) % ALIGN


def _check_path(path: str) -> bytes:
    if not path or path.startswith('/') or '\\' in path or '//' in path or path.endswith('/'):
        raise WwwImageError(f'bad asset path {path!r}: no leading or trailing slash, forward slashes only')
    if any(part in ('.', '..') for part in path.split('/')):
        raise WwwImageError(f'bad asset path {path!r}: no . or .. components')
    encoded = path.encode('utf-8')
    if len(encoded) > MAX_PATH_LEN or b'\0' in encoded:
        raise WwwImageError(f'bad asset path {path!r}: at most {MAX_PATH_LEN} bytes, no NUL')
    return encoded


def pack(entries: list[Entry]) -> bytes:
    """The image bytes for these entries, sorted by path. Raises WwwImageError on a bad or duplicate path."""
    ordered = sorted(entries, key=lambda e: e.path.encode('utf-8'))
    paths = [_check_path(e.path) for e in ordered]
    if len(set(paths)) != len(paths):
        raise WwwImageError('duplicate asset path')
    if len(ordered) > 0xFFFF:
        raise WwwImageError('too many assets')

    strings = bytearray()
    path_offsets = []
    for path in paths:
        path_offsets.append(len(strings))
        strings += path + b'\0'
    if len(strings) > 0xFFFF:
        raise WwwImageError('string table too large')

    dir_len = ENTRY_LEN * len(ordered) + len(strings)
    dir_len += _pad(dir_len)

    body_offset = HEADER_LEN + dir_len
    table = bytearray()
    bodies = bytearray()
    for entry, path_off in zip(ordered, path_offsets, strict=True):
        table += struct.pack(ENTRY_FORMAT, path_off, entry.flags, body_offset + len(bodies), len(entry.data), 0)
        bodies += entry.data
        bodies += b'\0' * _pad(len(entry.data))

    directory = bytes(table) + bytes(strings)
    directory += b'\0' * (dir_len - len(directory))
    after_header = directory + bytes(bodies)
    body_hash = hashlib.sha256(after_header).digest()[:BODY_HASH_LEN]
    header = struct.pack(HEADER_FORMAT, MAGIC, VERSION, len(ordered), dir_len, body_hash, b'\0' * 4)
    return header + after_header


@dataclass(frozen=True)
class Image:
    entries: tuple[Entry, ...]
    body_hash: bytes
    size: int  # bytes of the image, header to the end of the last body's padding

    def find(self, path: str) -> Entry | None:
        return next((e for e in self.entries if e.path == path), None)


def parse(data: bytes) -> Image:
    """Decode and fully validate an image (what the firmware checks at mount). Raises WwwImageError."""
    if len(data) < HEADER_LEN:
        raise WwwImageError('too short for a header')
    magic, version, count, dir_len, body_hash, _reserved = struct.unpack_from(HEADER_FORMAT, data, 0)
    if magic != MAGIC:
        raise WwwImageError(f'bad magic 0x{magic:08X}')
    if version != VERSION:
        raise WwwImageError(f'unsupported version {version}')
    if dir_len % ALIGN or dir_len < ENTRY_LEN * count or HEADER_LEN + dir_len > len(data):
        raise WwwImageError('directory length out of range')
    strings_start = HEADER_LEN + ENTRY_LEN * count
    strings = data[strings_start : HEADER_LEN + dir_len]
    body_start = HEADER_LEN + dir_len

    entries = []
    end = body_start
    previous = None
    for index in range(count):
        path_off, flags, offset, size, _reserved = struct.unpack_from(
            ENTRY_FORMAT, data, HEADER_LEN + ENTRY_LEN * index
        )
        if path_off >= len(strings):
            raise WwwImageError(f'entry {index}: path offset out of range')
        nul = strings.find(b'\0', path_off)
        if nul < 0:
            raise WwwImageError(f'entry {index}: unterminated path')
        path = strings[path_off:nul]
        if previous is not None and path <= previous:
            raise WwwImageError(f'entry {index}: paths not strictly increasing')
        previous = path
        if offset < body_start or offset % ALIGN or offset + size > len(data) or offset + size < offset:
            raise WwwImageError(f'entry {index}: body out of range')
        if flags & ~(FLAG_GZIP | FLAG_IMMUTABLE):
            raise WwwImageError(f'entry {index}: unknown flags 0x{flags:04X}')
        entries.append(
            Entry(
                path.decode('utf-8'),
                data[offset : offset + size],
                bool(flags & FLAG_GZIP),
                bool(flags & FLAG_IMMUTABLE),
            )
        )
        end = max(end, offset + size + _pad(size))

    if hashlib.sha256(data[HEADER_LEN:end]).digest()[:BODY_HASH_LEN] != body_hash:
        raise WwwImageError('body hash mismatch')
    return Image(tuple(entries), body_hash, end)


def main(argv: list[str] | None = None) -> int:
    """``wwwimage.py <dir> <out.bin> [--gzip]``: pack a directory as-is. Test fixtures only; the build uses gen_staticfs.py."""
    import argparse
    from pathlib import Path

    ap = argparse.ArgumentParser(description='Pack a directory into a www image without gzipping (test fixtures)')
    ap.add_argument('directory', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--gzip', action='store_true', help='mark every entry as gzipped (the bytes are taken as-is)')
    args = ap.parse_args(argv)

    entries = [
        Entry(
            p.relative_to(args.directory).as_posix(),
            p.read_bytes(),
            gzip=args.gzip,
            immutable='/immutable/' in f'/{p.relative_to(args.directory).as_posix()}',
        )
        for p in sorted(args.directory.rglob('*'))
        if p.is_file()
    ]
    args.output.write_bytes(pack(entries))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
