"""Tests for the build-variant grouping in scripts/utils/boards.py: which boards one compile can serve."""

import pytest
from support import write_fragment

from utils import boards

BASE = 'CONFIG_IDF_TARGET="esp32s3"\nCONFIG_PARTITION_TABLE_CUSTOM=y\n'


def vid(tmp_path, body):
    return boards.variant_id(write_fragment(tmp_path, 'board', body))


def variant_of(board):
    return boards.variant_id(boards.board_fragment(board))


def test_kconfig_lines_keep_settings_and_disabled_bools_only(tmp_path):
    fragment = write_fragment(
        tmp_path,
        'board',
        '# A comment\n\nCONFIG_IDF_TARGET="esp32"\nOPENSHOCK_RF_TX_GPIO=15\n  # CONFIG_FOO is not set\nCONFIG_BAR=y\n',
    )
    assert boards.kconfig_lines(fragment) == ['CONFIG_IDF_TARGET="esp32"', '# CONFIG_FOO is not set', 'CONFIG_BAR=y']


def test_variant_ignores_pins_flash_size_and_order(tmp_path):
    base = vid(tmp_path, BASE)
    assert base.startswith('esp32s3-')
    assert vid(tmp_path, BASE + 'OPENSHOCK_LED_GPIO=2\nOPENSHOCK_GPIO_BYPASS=15\n') == base
    assert vid(tmp_path, BASE + 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y\n') == base
    assert vid(tmp_path, BASE + '# CONFIG_ESPTOOLPY_FLASHSIZE_4MB is not set\n') == base
    assert vid(tmp_path, 'CONFIG_PARTITION_TABLE_CUSTOM=y\nCONFIG_IDF_TARGET="esp32s3"\n') == base
    # The table follows from the flash size, so naming one does not split a variant either
    assert vid(tmp_path, BASE + 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/legacy/ota_4mb_v1.csv"\n') == base
    assert vid(tmp_path, 'CONFIG_IDF_TARGET="esp32s3"\n') == base


@pytest.mark.parametrize(
    'extra',
    ['CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y\n', 'CONFIG_SPIRAM=y\n'],
)
def test_variant_splits_on_compiled_in_kconfig(tmp_path, extra):
    assert vid(tmp_path, BASE + extra) != vid(tmp_path, BASE)


def test_variant_splits_on_target(tmp_path):
    assert vid(tmp_path, BASE.replace('esp32s3', 'esp32')) != vid(tmp_path, BASE)


def test_every_repository_board_is_in_exactly_one_variant():
    names = boards.all_boards()
    variants = boards.group_variants(names)
    assert sorted(b for v in variants for b in v.boards) == names
    for variant in variants:
        for board in variant.boards:
            assert boards.read_target(boards.board_fragment(board)) == variant.target
            assert variant_of(board) == variant.id


def test_variants_follow_the_boards_asked_for():
    (only,) = boards.group_variants(['OpenShock-Core-V2'])
    assert only.boards == ('OpenShock-Core-V2',)
    assert only.target == 'esp32s3'
    assert boards.group_variants(['OpenShock-Core-V2', 'OpenShock-Core-V2']) == [only]


def test_s3_console_split_is_a_variant_split():
    # The serial driver and the bootloader console are compiled in, so the UART and the
    # USB-Serial-JTAG boards cannot share an image; flash size alone does not split them.
    assert variant_of('OpenShock-Core-V2') == variant_of('Wemos-Lolin-S3-Mini')
    assert variant_of('OpenShock-Core-V1') == variant_of('Wemos-Lolin-S3')
    assert variant_of('OpenShock-Core-V2') != variant_of('OpenShock-Core-V1')


def test_variant_fragment_compiles_at_the_variant_flash_size():
    for variant in boards.group_variants(boards.all_boards()):
        lines = variant.sdkconfig_fragment().splitlines()
        assert [line for line in lines if 'FLASHSIZE' in line] == [
            f'CONFIG_ESPTOOLPY_FLASHSIZE_{boards.VARIANT_FLASH_SIZE}=y'
        ], variant.id


def test_parse_flash_size_takes_the_last_selection():
    text = 'CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y\n# CONFIG_ESPTOOLPY_FLASHSIZE_2MB is not set\nCONFIG_ESPTOOLPY_FLASHSIZE_16MB=y\n'
    assert boards.parse_flash_size(text) == '16MB'
    assert boards.parse_flash_size('CONFIG_IDF_TARGET="esp32"\n') is None
