"""tools/sdlog/sdlog.py against the golden card image the firmware's own writer produced.

tools/sdlog/testdata/golden comes from the diagnostics host test ("Golden card image" in
components/diagnostics/host_test/main/test_sdlayout.cpp); regenerate it there when the format changes.
"""

import json
import shutil
import zlib
from pathlib import Path

import pytest
import sdlog

GOLDEN = Path(__file__).resolve().parents[2] / 'tools' / 'sdlog' / 'testdata' / 'golden'


@pytest.fixture
def card() -> sdlog.ImageDirSource:
    return sdlog.ImageDirSource(GOLDEN)


def test_golden_card_classifies_as_ours(card: sdlog.ImageDirSource) -> None:
    kind, info = sdlog.classify(card)
    assert kind == 'ours'
    assert info is not None
    assert info.epoch == 0x0BADC0DE
    assert info.ring_blocks == 1_564_664
    assert info.superblock.board == 'Host-Test-Board'
    assert info.superblock.fw_version == '1.6.0-test'


def test_head_and_records(card: sdlog.ImageDirSource) -> None:
    _, info = sdlog.classify(card)
    assert info is not None
    head = sdlog.recover_head(card, info)
    assert head.block is not None and head.block.seq == 2 and not head.full_scan

    scan = sdlog.scan_log(card, info, head)
    assert scan.blocks == 3 and not scan.invalid
    types = [r.type for r in scan.records]
    assert types[:2] == ['mount', 'boot']
    assert types.count('boot') == 2
    assert 'unmount' in types and 'dropped' in types and 'dump_stored' in types

    logs = [r for r in scan.records if r.type == 'log']
    assert logs[0].fields == {'level': 'I', 'tag': 'main', 'message': 'Board: Host-Test-Board (ESP32-S3)'}
    cut = next(r for r in logs if r.fields['tag'] == 'esp-tls')
    assert cut.truncated and set(cut.fields['message']) == {'z'} and len(cut.fields['message']) < 700
    assert [r.boot for r in logs][-1] == 1


def test_wall_time_only_within_a_synced_boot(card: sdlog.ImageDirSource) -> None:
    _, info = sdlog.classify(card)
    assert info is not None
    scan = sdlog.scan_log(card, info, sdlog.recover_head(card, info))
    walls = sdlog.wall_times(scan.records)

    rf = next(i for i, r in enumerate(scan.records) if r.type == 'log' and r.fields['tag'] == 'RFTransmitter')
    sync = next(r for r in scan.records if r.type == 'time_sync')
    expected = (sync.fields['unix_us'] + (6000 - 5000) * 1000) / 1e6
    assert walls[rf] is not None and walls[rf].timestamp() == pytest.approx(expected)

    panic = next(i for i, r in enumerate(scan.records) if r.type == 'log' and r.boot == 1)
    assert walls[panic] is None
    assert sdlog.format_time(scan.records[panic], None) == 'b1+1.500s'


def test_dump_command_writes_logs_and_the_coredump(tmp_path: Path) -> None:
    assert sdlog.main(['dump', str(GOLDEN), str(tmp_path)]) == 0

    text = (tmp_path / 'log.txt').read_text()
    assert 'E RFTransmitter: Failed to send command' in text
    assert 'b1+1.500s E main: PANIC: something broke' in text
    assert '--- crash dump 0 stored in slot 0 (3000 bytes, from boot 0)' in text

    lines = [json.loads(line) for line in (tmp_path / 'log.jsonl').read_text().splitlines()]
    assert any(e['type'] == 'dropped' and e['records'] == 3 and e['bytes'] == 150 for e in lines)

    dump = (tmp_path / 'coredumps' / 'b0_0.elf').read_bytes()
    assert dump == bytes((i * 7 + 3) & 0xFF for i in range(3000))

    summary = (tmp_path / 'summary.txt').read_text()
    assert 'boot 1: reset panic' in summary
    assert 'task loopTask, pc 0x400d1234' in summary
    assert 'torn or damaged blocks: 0' in summary


def test_image_round_trips(tmp_path: Path, card: sdlog.ImageDirSource) -> None:
    assert sdlog.cmd_image(str(GOLDEN), tmp_path / 'img') == 0
    copy = sdlog.ImageDirSource(tmp_path / 'img')
    for lba in (0, sdlog.DUMP_LBA, sdlog.LOG_LBA, sdlog.LOG_LBA + 8, sdlog.DUMP_LBA + 2048):
        assert copy.read(lba, 4) == card.read(lba, 4)


def test_damaged_superblock_pair_makes_the_card_foreign(tmp_path: Path) -> None:
    shutil.copytree(GOLDEN, tmp_path / 'card')
    # Rewrite the log superblock backup with a different (validly sealed) epoch: an interrupted format.
    extent = tmp_path / 'card' / 'extents' / f'{sdlog.LOG_LBA}.bin'
    data = bytearray(extent.read_bytes())
    backup = bytearray(data[512:1024])
    backup[12:16] = (0x12345678).to_bytes(4, 'little')
    backup[0x1FC:0x200] = zlib.crc32(bytes(backup[:0x1FC])).to_bytes(4, 'little')
    data[512:1024] = backup
    extent.write_bytes(bytes(data))
    assert sdlog.classify(sdlog.ImageDirSource(tmp_path / 'card'))[0] == 'foreign'


def test_blank_card(tmp_path: Path) -> None:
    (tmp_path / 'extents').mkdir()
    (tmp_path / 'card.json').write_text('{"card_sectors": 4194304}')
    assert sdlog.classify(sdlog.ImageDirSource(tmp_path)) == ('blank', None)
