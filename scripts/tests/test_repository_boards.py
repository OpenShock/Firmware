"""Every board in boards/ must be buildable, stampable, packageable and mergeable.

One parametrized case per board, so a broken fragment names itself rather than hiding the boards after it.
"""

import pytest

from utils import boards, partitions

ALL_BOARDS = boards.all_boards()


@pytest.mark.parametrize('board', ALL_BOARDS)
def test_board_is_complete(board):
    # Parsed whole: pins, flags, and a target utils/targets.py knows (so a chip label and an image chip id).
    boards.read_board(board)

    # The variant compiles at VARIANT_FLASH_SIZE, so no board may have less.
    assert boards.flash_size_bytes(boards.flash_size(board)) >= boards.flash_size_bytes(boards.VARIANT_FLASH_SIZE)

    csv = partitions.partition_csv_for(board)
    assert csv.is_file()
    assert 'www' in partitions.read_partitions(csv)
    assert partitions.partition_table_offset(board) == 0x8000
