"""Tests for the binary partition-table builder in scripts/utils/partitions.py and the per-flash-size table choice."""

import hashlib
import struct

import pytest
from support import ROOT

from utils import boards, partitions

TABLES = sorted((ROOT / 'partitions').glob('*.csv')) + sorted((ROOT / 'partitions' / 'legacy').glob('*.csv'))


def entries(table: bytes):
    out = []
    for pos in range(0, partitions.TABLE_LEN, partitions.ENTRY_LEN):
        magic, ptype, subtype, offset, size, label, flags = struct.unpack_from(partitions.ENTRY_FORMAT, table, pos)
        if magic != partitions.ENTRY_MAGIC:
            break
        out.append((ptype, subtype, offset, size, label.rstrip(b'\0').decode(), flags))
    return out


@pytest.mark.parametrize('csv', TABLES, ids=lambda p: p.stem)
def test_every_repository_table_builds_like_gen_esp32part(csv):
    # The exact layout gen_esp32part.py writes (checked against ESP-IDF 6.0 for these files): 32-byte entries in
    # file order, an MD5 entry over them, 0xFF to 0xC00.
    table = partitions.build_table(csv)
    assert len(table) == partitions.TABLE_LEN
    parsed = partitions.read_partitions(csv)
    assert [e[4] for e in entries(table)] == list(parsed)
    for (_ptype, _subtype, offset, size, label, flags), (name, part) in zip(
        entries(table), parsed.items(), strict=True
    ):
        assert (label, offset, size, flags) == (name, part['offset'], part['size'], 0)

    md5_pos = partitions.ENTRY_LEN * len(parsed)
    assert struct.unpack_from('<H', table, md5_pos)[0] == partitions.MD5_MAGIC
    assert table[md5_pos + 2 : md5_pos + 16] == b'\xff' * 14
    assert table[md5_pos + 16 : md5_pos + 32] == hashlib.md5(table[:md5_pos]).digest()
    assert table[md5_pos + 32 :] == b'\xff' * (partitions.TABLE_LEN - md5_pos - 32)


def test_type_and_subtype_codes():
    table = partitions.build_table(ROOT / 'partitions' / 'ota_4mb.csv')
    codes = {e[4]: (e[0], e[1]) for e in entries(table)}
    assert codes['nvs'] == (0x01, 0x02)
    assert codes['otadata'] == (0x01, 0x00)
    assert codes['app0'] == (0x00, 0x10)
    assert codes['app1'] == (0x00, 0x11)
    assert codes['cfg'] == (0x01, 0x02)
    assert codes['www'] == (0x40, 0x00)
    assert codes['coredump'] == (0x01, 0x03)


def test_the_migration_invariant_holds_across_every_table():
    # nvs, otadata and the app0 offset never move: the firmware rewrites the table in place relying on it.
    for csv in TABLES:
        table = partitions.read_partitions(csv)
        assert (table['nvs']['offset'], table['nvs']['size']) == (0x9000, 0x5000), csv
        assert (table['otadata']['offset'], table['otadata']['size']) == (0xE000, 0x2000), csv
        assert table['app0']['offset'] == 0x10000, csv


def test_the_2x_tables_have_what_the_firmware_hardcodes():
    for name, size in (('ota_4mb.csv', 4), ('ota_8mb.csv', 8)):
        table = partitions.read_partitions(ROOT / 'partitions' / name)
        assert table['cfg']['type'] == 'data' and table['cfg']['subtype'] == 'nvs'
        assert table['www']['type'] == '0x40'
        assert max(p['offset'] + p['size'] for p in table.values()) <= size * 1024 * 1024
        partitions.build_table(ROOT / 'partitions' / name, size * 1024 * 1024)  # fits
    # The 2.x app slots are no smaller than the 1.x ones, so a 1.x image always fits its grown slot
    legacy = partitions.read_partitions(ROOT / 'partitions' / 'legacy' / 'ota_4mb_v1.csv')
    for name in ('ota_4mb.csv', 'ota_8mb.csv'):
        table = partitions.read_partitions(ROOT / 'partitions' / name)
        assert table['app0']['size'] >= legacy['app0']['size']
        assert table['app1']['size'] >= legacy['app1']['size']


@pytest.mark.parametrize(
    'csv, error',
    [
        ('app0, app, ota_0, 0x10800, 0x10000\n', 'not 64 KiB aligned'),
        ('nvs, data, nvs, 0x9800, 0x5000\n', 'not 4 KiB aligned'),
        ('nvs, data, nvs, 0x9000, 0x4800\n', 'not a positive multiple of 4 KiB'),
        ('nvs, data, nvs, 0x9000, 0x5000\notadata, data, ota, 0xD000, 0x2000\n', 'overlaps the partition before it'),
        ('nvs, data, nvs, 0x9000, 0x5000\nbogus, data, nosuch, 0xE000, 0x2000\n', 'unknown subtype'),
        ('nvs, data, nvs, 0x9000, 0x5000\nbogus, nosuch, 0x0, 0xE000, 0x2000\n', 'unknown type'),
        ('averyveryverylonglabel, data, nvs, 0x9000, 0x5000\n', 'longer than 16 bytes'),
    ],
)
def test_a_table_the_bootloader_would_refuse_is_refused(tmp_path, csv, error):
    path = tmp_path / 'table.csv'
    path.write_text(csv, encoding='utf-8')
    with pytest.raises(SystemExit, match=error):
        partitions.build_table(path)


def test_an_entry_past_the_flash_is_refused(tmp_path):
    path = tmp_path / 'table.csv'
    path.write_text('app0, app, ota_0, 0x10000, 0x400000\n', encoding='utf-8')
    with pytest.raises(SystemExit, match='past the 4 MB flash'):
        partitions.build_table(path, 4 * 1024 * 1024)
    partitions.build_table(path, 8 * 1024 * 1024)


def test_the_table_follows_the_flash_size():
    assert partitions.table_csv_for_flash_size('4MB').name == 'ota_4mb.csv'
    assert partitions.table_csv_for_flash_size('8MB').name == 'ota_8mb.csv'
    assert partitions.table_csv_for_flash_size('16MB').name == 'ota_8mb.csv'
    with pytest.raises(SystemExit, match='at least 4 MB'):
        partitions.table_csv_for_flash_size('2MB')


def test_every_repository_board_gets_the_table_for_its_flash():
    for board in boards.all_boards():
        size = int(boards.flash_size(board).removesuffix('MB'))
        assert partitions.partition_csv_for(board).name == ('ota_8mb.csv' if size >= 8 else 'ota_4mb.csv'), board


def test_cli_writes_the_table(tmp_path):
    out = tmp_path / 'table.bin'
    assert partitions.main([str(ROOT / 'partitions' / 'ota_8mb.csv'), str(out), '--flash-size', '8MB']) == 0
    assert out.read_bytes() == partitions.build_table(ROOT / 'partitions' / 'ota_8mb.csv')
