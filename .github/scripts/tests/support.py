"""Shared test helpers: the repository root, synthetic app images and board fragments."""

import hashlib
import struct
from pathlib import Path

from utils import espimage

ROOT = Path(__file__).resolve().parents[3]


def blank(chip: str = 'ESP32-S3') -> bytes:
    """A packed board descriptor with no board stamped in."""
    return espimage.Descriptor('', chip, -1, -1, -1, -1, 0, 0).pack()


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
