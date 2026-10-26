#!/usr/bin/env python3
"""Read an OpenShock diagnostics SD card: logs and crash dumps, no filesystem.

The card format is docs/SDCARD.md; the firmware side is components/diagnostics/src/sdcard/SdLayout.cpp.
This reader parses with the same rules. Standard library only.

Commands:
    sdlog.py list                          removable disks, and whether each looks like an OpenShock card (admin)
    sdlog.py image <device> <dir>          copy the used parts of a card into an image directory (admin)
    sdlog.py dump <device-or-dir> <outdir> write log.txt, log.jsonl, coredumps/*.elf and summary.txt

Raw device paths: Linux /dev/sdX or /dev/mmcblkN (sudo); macOS /dev/rdiskN (sudo; click Ignore on the
"not readable" dialog, never Initialize); Windows \\\\.\\PhysicalDriveN (administrator terminal).

An image directory holds card.json ({"card_sectors": N}) and extents/<lba>.bin, each a run of sectors
starting at that LBA. Sectors in no extent read as zero. `image` writes one; `dump` reads either.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import platform
import plistlib
import struct
import subprocess
import sys
import zlib
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

SECTOR = 512

MIN_CARD_SECTORS = 1 << 21
DUMP_TYPE = 0xFA
DUMP_LBA = 8192
DUMP_SECTORS = 524_288
DUMP_DATA_START = 2048
SLOT_SECTORS = 2048
SLOT_COUNT = 255
LOG_TYPE = 0x7F
LOG_LBA = DUMP_LBA + DUMP_SECTORS
RING_DATA_START = 8
LAYOUT_VERSION = 1

LOG_SB_MAGIC = b'OSLOGSB\0'
DUMP_SB_MAGIC = b'OSDMPSB\0'
BLOCK_MAGIC = b'OBLK'
SLOT_MAGIC = b'ODMP'
PAYLOAD_SIZE = 476
PROBES = 64

# Superblock: magic, version, flags, epoch, unit_sectors, data_start, unit_count, partition_lba, partition_sectors,
# card_sectors, format_unix_s, mac, reserved, fw_version, board
SUPERBLOCK = struct.Struct('<8sHHIIIIIIIQ6sH32s32s')
# Block header: magic, epoch, seq, boot, payload_len, flags, uptime_ms
BLOCK = struct.Struct('<4sIQIHHQ')
# Slot header: magic, epoch, seq, stored_boot, origin_boot, object_type, flags, size, data_crc32, reset_reason,
# reserved, stored_uptime_ms, app_elf_sha256, stored_fw_version, board, exc_pc, exc_task
SLOT = struct.Struct('<4sIQIIHHIIB3sQ32s32s32sI16s')

LEVELS = {1: 'E', 2: 'W', 3: 'I', 4: 'D', 5: 'V'}
RESET_REASONS = {
    0: 'unknown',
    1: 'power-on',
    2: 'external',
    3: 'software',
    4: 'panic',
    5: 'interrupt watchdog',
    6: 'task watchdog',
    7: 'watchdog',
    8: 'deep sleep',
    9: 'brownout',
    10: 'sdio',
    11: 'usb',
    12: 'jtag',
    13: 'efuse',
    14: 'power glitch',
    15: 'cpu lockup',
}
TIME_SOURCES = {1: 'ntp', 2: 'backend', 3: 'rtc', 4: 'manual'}
MOUNT_REASONS = {1: 'boot', 2: 'reinserted'}


class CardError(Exception):
    pass


def sector_crc(sector: bytes) -> int:
    return zlib.crc32(sector[:0x1FC])


def stored_crc(sector: bytes) -> int:
    return int(struct.unpack_from('<I', sector, 0x1FC)[0])


def cstr(raw: bytes) -> str:
    return raw.split(b'\0', 1)[0].decode('utf-8', 'replace')


# ---------------------------------------------------------------------------------------------------------------------
# Sources


class Source:
    card_sectors: int | None

    def read(self, lba: int, count: int = 1) -> bytes:
        raise NotImplementedError


class DeviceSource(Source):
    """A raw device or a full image file."""

    def __init__(self, path: str) -> None:
        self._f = open(path, 'rb', buffering=0)  # noqa: SIM115 - lives as long as the source
        try:
            end = self._f.seek(0, os.SEEK_END)
        except OSError:
            end = 0
        self.card_sectors = end // SECTOR if end > 0 else None

    def read(self, lba: int, count: int = 1) -> bytes:
        self._f.seek(lba * SECTOR)
        data = self._f.read(count * SECTOR)
        if len(data) != count * SECTOR:
            raise CardError(f'short read at LBA {lba}')
        return data


class ImageDirSource(Source):
    """An image directory written by `image` (or the firmware's host test)."""

    def __init__(self, path: Path) -> None:
        meta = json.loads((path / 'card.json').read_text())
        self.card_sectors = int(meta['card_sectors'])
        self._extents: list[tuple[int, bytes]] = []
        for f in sorted((path / 'extents').glob('*.bin'), key=lambda p: int(p.stem)):
            self._extents.append((int(f.stem), f.read_bytes()))

    def read(self, lba: int, count: int = 1) -> bytes:
        out = bytearray(count * SECTOR)
        end = lba + count
        for start, data in self._extents:
            stop = start + len(data) // SECTOR
            lo, hi = max(lba, start), min(end, stop)
            if lo < hi:
                out[(lo - lba) * SECTOR : (hi - lba) * SECTOR] = data[(lo - start) * SECTOR : (hi - start) * SECTOR]
        return bytes(out)


def open_source(path: str) -> Source:
    p = Path(path)
    if p.is_dir():
        return ImageDirSource(p)
    return DeviceSource(path)


# ---------------------------------------------------------------------------------------------------------------------
# Classification


@dataclass
class Superblock:
    raw: bytes
    epoch: int
    unit_count: int
    partition_lba: int
    partition_sectors: int
    card_sectors: int
    format_unix_s: int
    mac: bytes
    fw_version: str
    board: str


@dataclass
class Card:
    card_sectors: int
    epoch: int
    log_sectors: int
    ring_blocks: int
    superblock: Superblock

    def ring_lba(self, position: int) -> int:
        return LOG_LBA + RING_DATA_START + position


def parse_superblock(
    sector: bytes, magic: bytes, lba: int, sectors: int, card_sectors: int | None
) -> Superblock | None:
    f = SUPERBLOCK.unpack_from(sector)
    if f[0] != magic or f[1] != LAYOUT_VERSION or stored_crc(sector) != sector_crc(sector):
        return None
    if f[7] != lba or f[8] != sectors or (card_sectors is not None and f[9] != card_sectors):
        return None
    return Superblock(sector, f[3], f[6], f[7], f[8], f[9], f[10], f[11], cstr(f[13]), cstr(f[14]))


def superblock_pair(src: Source, magic: bytes, lba: int, sectors: int) -> Superblock | None:
    primary, backup = (parse_superblock(src.read(lba + i), magic, lba, sectors, src.card_sectors) for i in (0, 1))
    if primary and backup:
        return primary if primary.raw == backup.raw else None  # An interrupted format
    return primary or backup


def classify(src: Source) -> tuple[str, Card | None]:
    """('ours', card), ('blank', None) or ('foreign', None), by the firmware's rules."""
    mbr = src.read(0)
    if mbr == bytes(SECTOR) or mbr == b'\xff' * SECTOR:
        return 'blank', None
    if mbr[0x1FE:0x200] != b'\x55\xaa':
        return 'foreign', None

    entries = [struct.unpack_from('<B3sB3sII', mbr, 0x1BE + 16 * i) for i in range(4)]
    dump, log = entries[0], entries[1]
    if (dump[2], dump[4], dump[5]) != (DUMP_TYPE, DUMP_LBA, DUMP_SECTORS):
        return 'foreign', None
    if log[2] != LOG_TYPE or log[4] != LOG_LBA or log[5] == 0:
        return 'foreign', None
    if src.card_sectors is not None and log[4] + log[5] > src.card_sectors:
        return 'foreign', None
    if mbr[0x1DE:0x1FE] != bytes(32):
        return 'foreign', None

    dump_sb = superblock_pair(src, DUMP_SB_MAGIC, DUMP_LBA, DUMP_SECTORS)
    log_sb = superblock_pair(src, LOG_SB_MAGIC, LOG_LBA, log[5])
    if not dump_sb or not log_sb or dump_sb.epoch != log_sb.epoch or dump_sb.epoch in (0, 0xFFFFFFFF):
        return 'foreign', None
    ring_blocks = (log[5] - RING_DATA_START) & ~7
    if log_sb.unit_count != ring_blocks or dump_sb.unit_count != SLOT_COUNT:
        return 'foreign', None

    card_sectors = src.card_sectors or log_sb.card_sectors
    return 'ours', Card(card_sectors, log_sb.epoch, log[5], ring_blocks, log_sb)


# ---------------------------------------------------------------------------------------------------------------------
# Ring


@dataclass
class Block:
    position: int
    seq: int
    boot: int
    uptime_ms: int
    payload: bytes


def block_status(sector: bytes, card: Card, position: int) -> tuple[str, Block | None]:
    """('valid', block), ('other-epoch', None) or ('invalid', None)."""
    magic, epoch, seq, boot, payload_len, _flags, uptime = BLOCK.unpack_from(sector)
    if magic != BLOCK_MAGIC or stored_crc(sector) != sector_crc(sector) or payload_len > PAYLOAD_SIZE:
        return 'invalid', None
    if epoch != card.epoch:
        return 'other-epoch', None
    if seq % card.ring_blocks != position:
        return 'invalid', None
    return 'valid', Block(position, seq, boot, uptime, sector[0x20 : 0x20 + payload_len])


def read_block(src: Source, card: Card, position: int) -> Block | None:
    return block_status(src.read(card.ring_lba(position)), card, position)[1]


def lap(block: Block | None, card: Card) -> int:
    return -1 if block is None else block.seq // card.ring_blocks


@dataclass
class Head:
    block: Block | None  # None: empty ring
    full_scan: bool = False


def recover_head(src: Source, card: Card) -> Head:
    """The firmware's head recovery (SdLayout.cpp RecoverHead): binary search over laps, 64 probes, full scan."""
    n = card.ring_blocks
    empty = torn_zero = False
    head_block: Block | None = None
    k = -1

    first = read_block(src, card, 0)
    if first is not None:
        k, head_block = lap(first, card), first
        lo, hi = 0, n
        while hi - lo > 1:
            mid = (lo + hi) // 2
            b = read_block(src, card, mid)
            if lap(b, card) == k:
                lo, head_block = mid, b
            else:
                hi = mid
    else:
        last = read_block(src, card, n - 1)
        if last is not None:
            k, head_block, torn_zero = lap(last, card), last, True
        else:
            empty = True

    consistent = True
    if head_block is not None:
        nxt = read_block(src, card, (head_block.position + 1) % n)
        if nxt is not None and nxt.position != head_block.position and nxt.seq == head_block.seq + 1:
            consistent = False
    head_pos = head_block.position if head_block else 0
    for i in range(1, PROBES):
        if not consistent:
            break
        pos = i * n // PROBES
        if pos == 0 or (not empty and pos == head_pos):
            continue
        probe = lap(read_block(src, card, pos), card)
        if empty:
            consistent = probe < 0
        elif pos <= head_pos and not (torn_zero and pos == 0):
            consistent = probe == k
        else:
            consistent = probe < 0 or probe == k - 1

    if consistent:
        return Head(None if empty else head_block)

    print(f'note: ring damaged, scanning all {n} blocks', file=sys.stderr)
    best: Block | None = None
    for pos, sector in iter_ring(src, card, range(n)):
        status, b = block_status(sector, card, pos)
        if b is not None and (best is None or b.seq > best.seq):
            best = b
    return Head(best, full_scan=True)


def iter_ring(src: Source, card: Card, positions: range, chunk: int = 2048) -> Iterator[tuple[int, bytes]]:
    """(position, sector) for consecutive positions, read in chunks."""
    for start in range(positions.start, positions.stop, chunk):
        count = min(chunk, positions.stop - start)
        data = src.read(card.ring_lba(start), count)
        for i in range(count):
            yield start + i, data[i * SECTOR : (i + 1) * SECTOR]


def ring_order(src: Source, card: Card, head: Head) -> list[range]:
    """Positions oldest first: from head + 1 if that holds the previous lap, otherwise from 0."""
    if head.block is None:
        return []
    h = head.block.position
    n = card.ring_blocks
    nxt = read_block(src, card, (h + 1) % n)
    if h + 1 < n and nxt is not None and lap(nxt, card) == lap(head.block, card) - 1:
        return [range(h + 1, n), range(0, h + 1)]
    return [range(0, h + 1)]


# ---------------------------------------------------------------------------------------------------------------------
# Records


@dataclass
class Record:
    seq: int
    boot: int
    uptime_ms: int
    type: str
    fields: dict[str, Any] = field(default_factory=dict)
    truncated: bool = False


def short_string(data: bytes, pos: int) -> tuple[str, int]:
    n = data[pos]
    return data[pos + 1 : pos + 1 + n].decode('utf-8', 'replace'), pos + 1 + n


def parse_records(block: Block) -> list[Record]:
    out: list[Record] = []
    p, data = 0, block.payload
    while p + 4 <= len(data):
        rtype, flags, length = struct.unpack_from('<BBH', data, p)
        if rtype in (0x00, 0xFF) or p + 4 + length > len(data):
            break
        d = data[p + 4 : p + 4 + length]
        p += 4 + length
        rec = Record(block.seq, block.boot, block.uptime_ms, f'0x{rtype:02x}', truncated=bool(flags & 1))
        if rtype == 0x01 and len(d) >= 6:
            delta, level, tag_len = struct.unpack_from('<IBB', d)
            rec.type, rec.uptime_ms = 'log', block.uptime_ms + delta
            rec.fields = {
                'level': LEVELS.get(level, str(level)),
                'tag': d[6 : 6 + tag_len].decode('utf-8', 'replace'),
                'message': d[6 + tag_len :].decode('utf-8', 'replace'),
            }
        elif rtype == 0x02 and len(d) >= 10:
            pos = 10
            strings = []
            for _ in range(4):
                s, pos = short_string(d, pos)
                strings.append(s)
            rec.type = 'boot'
            rec.fields = {
                'reset_reason': RESET_REASONS.get(d[0], str(d[0])),
                'app_slot': d[1],
                'mac': d[4:10].hex(':'),
                'fw_version': strings[0],
                'board': strings[1],
                'variant': strings[2],
                'idf_version': strings[3],
            }
        elif rtype == 0x03 and len(d) >= 13:
            delta, unix_us, source = struct.unpack_from('<IqB', d)
            rec.type, rec.uptime_ms = 'time_sync', block.uptime_ms + delta
            rec.fields = {'unix_us': unix_us, 'source': TIME_SOURCES.get(source, str(source))}
        elif rtype == 0x04 and len(d) >= 24:
            obj_seq, slot, obj_type, _, size, origin = struct.unpack_from('<QIHHII', d)
            rec.type = 'dump_stored'
            rec.fields = {
                'object_seq': obj_seq,
                'slot': slot,
                'object_type': obj_type,
                'size': size,
                'origin_boot': origin,
            }
        elif rtype == 0x05 and len(d) >= 8:
            records, nbytes = struct.unpack_from('<II', d)
            rec.type, rec.fields = 'dropped', {'records': records, 'bytes': nbytes}
        elif rtype == 0x06 and len(d) >= 1:
            rec.type, rec.fields = 'mount', {'reason': MOUNT_REASONS.get(d[0], str(d[0]))}
        elif rtype == 0x07:
            rec.type = 'unmount'
        else:
            rec.fields = {'data': d.hex()}  # Unknown or application type: kept, skipped by len
        out.append(rec)
    return out


@dataclass
class LogScan:
    records: list[Record]
    blocks: int = 0
    invalid: list[int] = field(default_factory=list)  # Positions torn or damaged inside the run
    other_epoch: list[int] = field(default_factory=list)


def scan_log(src: Source, card: Card, head: Head) -> LogScan:
    scan = LogScan([])
    for run in ring_order(src, card, head):
        for pos, sector in iter_ring(src, card, run):
            status, block = block_status(sector, card, pos)
            if block is None:
                (scan.other_epoch if status == 'other-epoch' else scan.invalid).append(pos)
                continue
            scan.blocks += 1
            scan.records.extend(parse_records(block))
    return scan


def wall_times(records: list[Record]) -> dict[int, datetime.datetime | None]:
    """Wall time per record index, from the nearest TIME_SYNC in the same boot (None when the boot has none)."""
    syncs: dict[int, list[tuple[int, int]]] = {}
    for r in records:
        if r.type == 'time_sync':
            syncs.setdefault(r.boot, []).append((r.uptime_ms, r.fields['unix_us']))
    out: dict[int, datetime.datetime | None] = {}
    for i, r in enumerate(records):
        boot_syncs = syncs.get(r.boot)
        if not boot_syncs:
            out[i] = None
            continue
        up, unix_us = min(boot_syncs, key=lambda s: abs(s[0] - r.uptime_ms))
        out[i] = datetime.datetime.fromtimestamp((unix_us + (r.uptime_ms - up) * 1000) / 1e6, datetime.timezone.utc)
    return out


def format_time(r: Record, wall: datetime.datetime | None) -> str:
    if wall is not None:
        return wall.isoformat(timespec='milliseconds').replace('+00:00', 'Z')
    return f'b{r.boot}+{r.uptime_ms // 1000}.{r.uptime_ms % 1000:03d}s'


def format_record(r: Record, wall: datetime.datetime | None) -> str:
    t = format_time(r, wall)
    f = r.fields
    if r.type == 'log':
        cut = ' [truncated]' if r.truncated else ''
        return f'{t} {f["level"]} {f["tag"]}: {f["message"]}{cut}'
    if r.type == 'boot':
        return f'{t} --- boot {r.boot}: reset {f["reset_reason"]}, firmware {f["fw_version"]}, board {f["board"]}'
    if r.type == 'time_sync':
        when = datetime.datetime.fromtimestamp(f['unix_us'] / 1e6, datetime.timezone.utc).isoformat()
        return f'{t} --- time sync ({f["source"]}): {when}'
    if r.type == 'dump_stored':
        return f'{t} --- crash dump {f["object_seq"]} stored in slot {f["slot"]} ({f["size"]} bytes, from boot {f["origin_boot"]})'
    if r.type == 'dropped':
        return f'{t} --- {f["records"]} records ({f["bytes"]} bytes) dropped'
    if r.type == 'mount':
        return f'{t} --- card mounted ({f["reason"]})'
    if r.type == 'unmount':
        return f'{t} --- card ejected'
    return f'{t} --- record {r.type}: {f.get("data", "")}'


# ---------------------------------------------------------------------------------------------------------------------
# Dumps


@dataclass
class Dump:
    slot: int
    seq: int
    stored_boot: int
    origin_boot: int
    object_type: int
    size: int
    data_crc: int
    reset_reason: int
    stored_uptime_ms: int
    app_elf_sha256: bytes
    fw_version: str
    board: str
    exc_pc: int
    exc_task: str

    @property
    def lba(self) -> int:
        return DUMP_LBA + DUMP_DATA_START + self.slot * SLOT_SECTORS


def scan_dumps(src: Source, card: Card) -> list[Dump]:
    dumps = []
    for slot in range(SLOT_COUNT):
        sector = src.read(DUMP_LBA + DUMP_DATA_START + slot * SLOT_SECTORS)
        f = SLOT.unpack_from(sector)
        if (
            f[0] != SLOT_MAGIC
            or f[1] != card.epoch
            or stored_crc(sector) != sector_crc(sector)
            or f[2] % SLOT_COUNT != slot
        ):
            continue
        dumps.append(
            Dump(
                slot,
                f[2],
                f[3],
                f[4],
                f[5],
                f[7],
                f[8],
                f[9],
                f[11],
                f[12],
                cstr(f[13]),
                cstr(f[14]),
                f[15],
                cstr(f[16]),
            )
        )
    return sorted(dumps, key=lambda d: d.seq)


def dump_data(src: Source, dump: Dump) -> bytes:
    sectors = (dump.size + SECTOR - 1) // SECTOR
    return src.read(dump.lba + 1, sectors)[: dump.size] if sectors else b''


# ---------------------------------------------------------------------------------------------------------------------
# Commands


def require_ours(src: Source) -> Card:
    kind, card = classify(src)
    if card is None:
        raise CardError(f'not an OpenShock card ({kind})')
    return card


def cmd_dump(source: str, outdir: Path) -> int:
    src = open_source(source)
    card = require_ours(src)
    head = recover_head(src, card)
    scan = scan_log(src, card, head)
    walls = wall_times(scan.records)
    dumps = scan_dumps(src, card)

    outdir.mkdir(parents=True, exist_ok=True)
    with (
        open(outdir / 'log.txt', 'w', encoding='utf-8') as txt,
        open(outdir / 'log.jsonl', 'w', encoding='utf-8') as jl,
    ):
        for i, r in enumerate(scan.records):
            txt.write(format_record(r, walls[i]) + '\n')
            entry = {'seq': r.seq, 'boot': r.boot, 'uptime_ms': r.uptime_ms, 'type': r.type, **r.fields}
            if walls[i] is not None:
                entry['time'] = format_time(r, walls[i])
            if r.truncated:
                entry['truncated'] = True
            jl.write(json.dumps(entry, ensure_ascii=False) + '\n')

    dump_lines = []
    if dumps:
        (outdir / 'coredumps').mkdir(exist_ok=True)
    for d in dumps:
        data = dump_data(src, d)
        ok = zlib.crc32(data) == d.data_crc
        name = f'b{d.origin_boot}_{d.seq}.elf'
        if ok:
            (outdir / 'coredumps' / name).write_bytes(data)
        dump_lines.append(
            f'  {name}: {d.size} bytes, slot {d.slot}, task {d.exc_task or "?"}, pc 0x{d.exc_pc:08x}, '
            f'elf {d.app_elf_sha256.hex()}, stored by {d.fw_version} on {d.board}, '
            f'reset {RESET_REASONS.get(d.reset_reason, d.reset_reason)}'
            + ('' if ok else ' - DATA CRC MISMATCH, not written')
        )

    (outdir / 'summary.txt').write_text(summary(card, head, scan, walls, dump_lines), encoding='utf-8')
    print(f'{len(scan.records)} records from {scan.blocks} blocks, {len(dumps)} crash dumps -> {outdir}')
    return 0


def summary(
    card: Card, head: Head, scan: LogScan, walls: dict[int, datetime.datetime | None], dump_lines: list[str]
) -> str:
    sb = card.superblock
    formatted = (
        datetime.datetime.fromtimestamp(sb.format_unix_s, datetime.timezone.utc).isoformat()
        if sb.format_unix_s
        else 'unknown'
    )
    lines = [
        'Card',
        f'  size: {card.card_sectors * SECTOR / 2**30:.2f} GiB ({card.card_sectors} sectors)',
        f'  epoch: {card.epoch:08x}, formatted {formatted} by {sb.fw_version} on {sb.board} ({sb.mac.hex(":")})',
        f'  ring: {card.ring_blocks} blocks, head seq {head.block.seq if head.block else "-"}'
        + (' (recovered by full scan)' if head.full_scan else ''),
        '',
        'Boots',
    ]
    unclean, mounted = 0, False
    dropped_records = dropped_bytes = 0
    for r in scan.records:
        if r.type == 'boot':
            lines.append(f'  boot {r.boot}: reset {r.fields["reset_reason"]}, firmware {r.fields["fw_version"]}')
        elif r.type == 'mount':
            if mounted and r.fields['reason'] == 'reinserted':
                unclean += 1
            mounted = True
        elif r.type == 'unmount':
            mounted = False
        elif r.type == 'dropped':
            dropped_records += r.fields['records']
            dropped_bytes += r.fields['bytes']

    synced = [walls[i] for i in walls if walls[i] is not None]
    unsynced_boots = sorted({r.boot for i, r in enumerate(scan.records) if walls[i] is None})
    lines += [
        '',
        'Time',
        f'  synced: {min(synced).isoformat()} .. {max(synced).isoformat()}' if synced else '  synced: none',
        f'  unsynced boots: {", ".join(map(str, unsynced_boots)) or "none"}',
        '',
        'Integrity',
        f'  records: {len(scan.records)} in {scan.blocks} blocks',
        f'  dropped: {dropped_records} records, {dropped_bytes} bytes',
        f'  torn or damaged blocks: {len(scan.invalid)}'
        + (f' (positions {scan.invalid[:20]})' if scan.invalid else ''),
        f'  blocks of another format: {len(scan.other_epoch)}',
        f'  unclean removals: {unclean}',
        '',
        'Crash dumps',
        *(dump_lines or ['  none']),
        '',
    ]
    return '\n'.join(lines)


def cmd_image(device: str, outdir: Path) -> int:
    src = open_source(device)
    card = require_ours(src)
    head = recover_head(src, card)

    extents: list[tuple[int, int]] = [(0, 1), (DUMP_LBA, 2), (LOG_LBA, 2)]
    for d in scan_dumps(src, card):
        extents.append((d.lba, 1 + (d.size + SECTOR - 1) // SECTOR))
    for run in ring_order(src, card, head):  # Before a wrap: blocks 0..head only
        extents.append((card.ring_lba(run.start), len(run)))

    (outdir / 'extents').mkdir(parents=True, exist_ok=True)
    (outdir / 'card.json').write_text(json.dumps({'card_sectors': card.card_sectors, 'sector_size': SECTOR}) + '\n')
    total = 0
    for lba, count in extents:
        with open(outdir / 'extents' / f'{lba}.bin', 'wb') as f:
            for start in range(lba, lba + count, 2048):
                f.write(src.read(start, min(2048, lba + count - start)))
        total += count
    print(f'{total} sectors ({total * SECTOR / 2**20:.1f} MiB) -> {outdir}')
    return 0


def removable_disks() -> list[tuple[str, int | None, str]]:
    """(path, sectors, description) of the removable disks this OS reports."""
    system = platform.system()
    disks: list[tuple[str, int | None, str]] = []
    if system == 'Linux':
        for dev in sorted(Path('/sys/block').iterdir()):
            removable = (dev / 'removable').read_text().strip() == '1' if (dev / 'removable').exists() else False
            if removable or dev.name.startswith('mmcblk'):
                size = int((dev / 'size').read_text())
                model = (dev / 'device' / 'model').read_text().strip() if (dev / 'device' / 'model').exists() else ''
                disks.append((f'/dev/{dev.name}', size, model))
    elif system == 'Darwin':
        listing = plistlib.loads(subprocess.check_output(['diskutil', 'list', '-plist', 'external', 'physical']))
        for name in listing.get('WholeDisks', []):
            info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', name]))
            disks.append(
                (f'/dev/r{name}', int(info.get('TotalSize', 0)) // SECTOR or None, str(info.get('MediaName', '')))
            )
    elif system == 'Windows':
        ps = 'Get-CimInstance Win32_DiskDrive | Where-Object MediaType -like "*Removable*" | Select-Object DeviceID,Size,Model | ConvertTo-Json'
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command', ps], text=True).strip()
        items = json.loads(raw) if raw else []
        for item in items if isinstance(items, list) else [items]:
            disks.append(
                (item['DeviceID'], int(item['Size']) // SECTOR if item.get('Size') else None, item.get('Model') or '')
            )
    else:
        raise CardError(f'listing disks is not supported on {system}')
    return disks


def cmd_list() -> int:
    disks = removable_disks()
    if not disks:
        print('no removable disks found')
    for path, sectors, model in disks:
        size = f'{sectors * SECTOR / 1e9:.1f} GB' if sectors else '? GB'
        try:
            kind, card = classify(DeviceSource(path))
            what = f'OpenShock card (epoch {card.epoch:08x}, formatted on {card.superblock.board})' if card else kind
        except PermissionError:
            what = 'unreadable (run as administrator)'
        except (OSError, CardError) as e:
            what = f'unreadable ({e})'
        print(f'{path}  {size}  {model}  {what}')
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)
    sub.add_parser('list', help='removable disks and whether each is an OpenShock card')
    p = sub.add_parser('image', help='copy the used parts of a card into an image directory')
    p.add_argument('device')
    p.add_argument('dir', type=Path)
    p = sub.add_parser('dump', help='write logs and crash dumps from a card or an image directory')
    p.add_argument('source')
    p.add_argument('outdir', type=Path)
    args = ap.parse_args(argv)

    try:
        if args.command == 'list':
            return cmd_list()
        if args.command == 'image':
            return cmd_image(args.device, args.dir)
        return cmd_dump(args.source, args.outdir)
    except (CardError, OSError) as e:
        print(f'error: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
