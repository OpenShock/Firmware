import os
from pathlib import Path
from utils.boardconf import BoardConf, from_pio_env, validate, print_header, print_footer

#
# This file is responsible for processing the "custom_openshock.*"
# parameters declared in `platformio.ini`.
#
# This file is invoked by PlatformIO during build.
# See 'extra_scripts' in 'platformio.ini'.
#

Import('env')  # type: ignore
env = env  # type: ignore

# Parse the board/chip specific configuration from the current pio `env` variable.
boardconf: BoardConf = from_pio_env(env)


def use_openshock_params():
    if not validate(boardconf):
        return

    # Handle partitions file not existing
    if not boardconf.does_partitions_file_exists():
        print('WARNING: PARTITIONS FILE DOES NOT EXIST.')
        print('Not overriding default value.')
        return

    # Set partition file to chip dir.
    # https://docs.platformio.org/en/latest/scripting/examples/override_board_configuration.html
    board_config = env.BoardConfig()
    board_config.update('build.partitions', str(boardconf.get_partitions_file()))


print_header()
use_openshock_params()
print_footer()
