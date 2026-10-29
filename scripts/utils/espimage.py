"""ESP-IDF application image parsing and the OpenShock board descriptor inside it.

Image layout (components/bootloader_support/include/esp_app_format.h):

    0x00  24-byte image header: magic 0xE9, segment count, flash mode/size, entry point,
          chip id (u16 at 0x0C), ... , `hash appended` flag at 0x17.
    0x18  first segment header (load address, length), then its data. ESP-IDF's linker
          script puts the app descriptor (esp_app_desc_t, magic 0xABCD5432, 256 bytes)
          first in that segment, at image offset 0x20, and the `.rodata_custom_desc`
          input section immediately after it, at 0x120. That is where the OpenShock
          board descriptor (BoardDescriptor.h) lives.
    ...   further segments.
    end   padding so the checksum byte lands at a 16-byte boundary minus one, the XOR
          checksum of every segment's data (initial 0xEF), then - when the header flag
          says so - the SHA-256 of everything before it.

The bootloader verifies both the checksum and the digest when it validates an OTA image,
so anything that changes a byte of a segment must recompute both. `patch()` does.
"""

import hashlib
import os
import struct
import zlib
from collections.abc import Iterator
from dataclasses import dataclass, replace
from functools import reduce
from operator import xor
from pathlib import Path

IMAGE_MAGIC = 0xE9
HEADER_LEN = 24
MAX_SEGMENTS = 16
CHIP_ID_OFFSET = 0x0C
HASH_FLAG_OFFSET = 0x17
SEGMENT_HEADER_LEN = 8
CHECKSUM_INIT = 0xEF
DIGEST_LEN = 32

APP_DESC_OFFSET = 0x20
APP_DESC_MAGIC = 0xABCD5432
APP_DESC_LEN = 256

# Mirrors OpenShock::BoardDescriptor (components/common/include/BoardDescriptor.h):
#   u32 magic, u16 schema_version, u16 length, char board[48], char chip[16],
#   i8 rf_tx_gpio, i8 estop_gpio, i8 led_gpio, i8 ws2812b_gpio,
#   u32 flags, u64 gpio_bypass_mask,
#   u8 eth_phy, i8 eth_phy_addr, i8 eth_power_gpio, i8 eth_mdc_gpio, i8 eth_mdio_gpio,
#   u8 reserved[159], u32 crc32 (of the first 252 bytes)
# The eth_* fields came out of `reserved` without a schema bump: eth_phy 0 means no Ethernet, which is what every
# descriptor stamped before them reads as, and firmware that predates them ignores those bytes.
DESC_OFFSET = APP_DESC_OFFSET + APP_DESC_LEN  # 0x120
DESC_MAGIC = 0x4442534F  # 'OSBD' as little-endian bytes
DESC_SCHEMA_VERSION = 1
DESC_LEN = 256
DESC_FORMAT = '<IHH48s16sbbbbIQBbbbb159sI'
DESC_CRC_LEN = DESC_LEN - 4
DESC_BOARD_LEN = 48
DESC_CHIP_LEN = 16


class DescriptorError(ValueError):
    pass


class ImageError(ValueError):
    pass


@dataclass(frozen=True)
class Descriptor:
    board: str
    chip: str
    rf_tx_gpio: int
    estop_gpio: int
    led_gpio: int
    ws2812b_gpio: int
    flags: int
    gpio_bypass_mask: int
    eth_phy: int = 0
    eth_phy_addr: int = 0
    eth_power_gpio: int = 0
    eth_mdc_gpio: int = 0
    eth_mdio_gpio: int = 0
    schema_version: int = DESC_SCHEMA_VERSION

    def pack(self) -> bytes:
        board = self.board.encode('ascii')
        chip = self.chip.encode('ascii')
        # An empty name is the unstamped placeholder a fresh build carries; a stamp always names a board.
        if len(board) >= DESC_BOARD_LEN:
            raise ValueError(f'board name must be at most {DESC_BOARD_LEN - 1} ASCII characters: {self.board!r}')
        if not chip or len(chip) >= DESC_CHIP_LEN:
            raise ValueError(f'chip label must be 1-{DESC_CHIP_LEN - 1} ASCII characters: {self.chip!r}')
        body = struct.pack(
            DESC_FORMAT,
            DESC_MAGIC,
            self.schema_version,
            DESC_LEN,
            board,
            chip,
            self.rf_tx_gpio,
            self.estop_gpio,
            self.led_gpio,
            self.ws2812b_gpio,
            self.flags,
            self.gpio_bypass_mask,
            self.eth_phy,
            self.eth_phy_addr,
            self.eth_power_gpio,
            self.eth_mdc_gpio,
            self.eth_mdio_gpio,
            b'',
            0,
        )
        crc = zlib.crc32(body[:DESC_CRC_LEN]) & 0xFFFFFFFF
        return body[:DESC_CRC_LEN] + struct.pack('<I', crc)


def unpack_descriptor(data: bytes) -> Descriptor:
    """Decode and validate 256 descriptor bytes. Raises DescriptorError when they are not a descriptor."""
    if len(data) < DESC_LEN:
        raise DescriptorError(f'need {DESC_LEN} bytes, got {len(data)}')
    data = bytes(data[:DESC_LEN])
    (
        magic,
        schema,
        length,
        board,
        chip,
        rf,
        estop,
        led,
        ws,
        flags,
        bypass,
        eth_phy,
        eth_addr,
        eth_power,
        eth_mdc,
        eth_mdio,
        _reserved,
        crc,
    ) = struct.unpack(DESC_FORMAT, data)
    if magic != DESC_MAGIC:
        raise DescriptorError(f'bad magic 0x{magic:08X}')
    if length != DESC_LEN:
        raise DescriptorError(f'bad length {length}')
    if schema != DESC_SCHEMA_VERSION:
        raise DescriptorError(f'unsupported schema version {schema}')
    if crc != (zlib.crc32(data[:DESC_CRC_LEN]) & 0xFFFFFFFF):
        raise DescriptorError('crc mismatch')
    return Descriptor(
        board=board.split(b'\0', 1)[0].decode('ascii', errors='replace'),
        chip=chip.split(b'\0', 1)[0].decode('ascii', errors='replace'),
        rf_tx_gpio=rf,
        estop_gpio=estop,
        led_gpio=led,
        ws2812b_gpio=ws,
        flags=flags,
        gpio_bypass_mask=bypass,
        eth_phy=eth_phy,
        eth_phy_addr=eth_addr,
        eth_power_gpio=eth_power,
        eth_mdc_gpio=eth_mdc,
        eth_mdio_gpio=eth_mdio,
        schema_version=schema,
    )


@dataclass(frozen=True)
class Segment:
    load_addr: int
    offset: int  # of the data, in the image
    length: int


@dataclass(frozen=True)
class Image:
    data: bytes
    chip_id: int
    hash_appended: bool
    segments: tuple[Segment, ...]
    checksum_offset: int

    @property
    def digest_offset(self) -> int | None:
        return self.checksum_offset + 1 if self.hash_appended else None

    def segment_bytes(self) -> Iterator[bytes]:
        for seg in self.segments:
            yield self.data[seg.offset : seg.offset + seg.length]

    def checksum(self) -> int:
        return reduce(xor, b''.join(self.segment_bytes()), CHECKSUM_INIT)

    def digest(self) -> bytes:
        return hashlib.sha256(self.data[: self.digest_offset]).digest()

    def verify(self) -> None:
        """Raise ImageError unless the stored checksum (and digest, if present) match the contents."""
        stored = self.data[self.checksum_offset]
        if stored != self.checksum():
            raise ImageError(f'checksum mismatch: stored 0x{stored:02X}, computed 0x{self.checksum():02X}')
        if self.hash_appended and self.data[self.digest_offset :] != self.digest():
            raise ImageError('sha256 digest mismatch')

    def descriptor_bytes(self) -> bytes:
        return self.data[DESC_OFFSET : DESC_OFFSET + DESC_LEN]

    def patch(self, offset: int, replacement: bytes) -> 'Image':
        """A copy with `replacement` written at `offset` and the checksum and digest recomputed.

        The write must stay inside one segment: the checksum covers segment data only, so
        touching a header or the padding would corrupt the image silently.
        """
        end = offset + len(replacement)
        if not any(seg.offset <= offset and end <= seg.offset + seg.length for seg in self.segments):
            raise ImageError(f'patch at 0x{offset:X}+{len(replacement)} does not lie inside a single segment')
        data = bytearray(self.data)
        data[offset:end] = replacement
        # Only segment bytes changed, so the layout - and with it the checksum and digest offsets - is ours.
        data[self.checksum_offset] = replace(self, data=bytes(data)).checksum()
        if self.hash_appended:
            data[self.digest_offset :] = replace(self, data=bytes(data)).digest()
        patched = parse(bytes(data))
        patched.verify()
        return patched


def parse(data: bytes) -> Image:
    """Parse the image structure. Does not check the checksum or digest; call verify() for that."""
    if len(data) < HEADER_LEN + SEGMENT_HEADER_LEN:
        raise ImageError(f'image too small ({len(data)} bytes)')
    if data[0] != IMAGE_MAGIC:
        raise ImageError(f'bad image magic 0x{data[0]:02X}')
    segment_count = data[1]
    if segment_count == 0 or segment_count > MAX_SEGMENTS:
        raise ImageError(f'unreasonable segment count {segment_count}')
    chip_id = struct.unpack_from('<H', data, CHIP_ID_OFFSET)[0]
    hash_appended = data[HASH_FLAG_OFFSET] == 1

    segments = []
    cursor = HEADER_LEN
    for index in range(segment_count):
        if cursor + SEGMENT_HEADER_LEN > len(data):
            raise ImageError(f'segment {index}: header runs past the end of the image')
        load_addr, length = struct.unpack_from('<II', data, cursor)
        cursor += SEGMENT_HEADER_LEN
        if cursor + length > len(data):
            raise ImageError(f'segment {index}: data runs past the end of the image')
        segments.append(Segment(load_addr, cursor, length))
        cursor += length

    # The checksum byte is the last byte of the 16-byte-aligned block the segments end in.
    checksum_offset = cursor + (-(cursor + 1) % 16)
    expected_len = checksum_offset + 1 + (DIGEST_LEN if hash_appended else 0)
    if len(data) != expected_len:
        raise ImageError(f'image is {len(data)} bytes, expected {expected_len} from its segment table')

    return Image(
        data=data,
        chip_id=chip_id,
        hash_appended=hash_appended,
        segments=tuple(segments),
        checksum_offset=checksum_offset,
    )


def load(path: str | os.PathLike[str]) -> Image:
    return parse(Path(path).read_bytes())
