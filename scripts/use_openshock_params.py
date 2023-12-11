import os
from pathlib import Path
from utils.boardconf import BoardConf, from_pio_env, validate, print_header, print_footer, fallback_flash_size
import csv

#
# This file is responsible for processing the "custom_openshock.*"
# parameters declared in `platformio.ini`.
#
# And for updating the `partitions.csv` file to include the
# `config` partition.
#
# This file is invoked by PlatformIO during build.
# See 'extra_scripts' in 'platformio.ini'.
#

Import('env')  # type: ignore
env = env  # type: ignore

# Parse the board/chip specific configuration from the current pio `env` variable.
boardconf: BoardConf = from_pio_env(env)

# In case we need to blacklist certain flash sizes from OTA support.
blacklisted_OTA_sizes = [
    # '4MB'
    ]

def use_openshock_params():
    if not validate(boardconf):
        return

    # Handle partitions file not existing
    if not boardconf.do_partitions_files_exists(boardconf.get_flash_size()):
        if boardconf.do_partitions_files_exists(fallback_flash_size):
            print(f'WARNING: PARTITIONS FILES DON\'T EXIST FOR THIS FLASH SIZE, FALLING BACK TO {fallback_flash_size}.')
            boardconf.override_flash_size(fallback_flash_size)

    # Set partition file to chip dir.
    # https://docs.platformio.org/en/latest/scripting/examples/override_board_configuration.html
    board_config = env.BoardConfig()

    partitions, partitions_ota = boardconf.get_partitions_files(boardconf.get_flash_size())

    if boardconf.get_flash_size() in blacklisted_OTA_sizes:
        print('WARNING: OTA NOT SUPPORTED FOR THIS FLASH SIZE.')
        print('Using Non-OTA partition layout.')
        board_config.update('build.partitions', str(partitions))
    else:   
        board_config.update('build.partitions', str(partitions_ota))


print_header()
use_openshock_params()
print_footer()
