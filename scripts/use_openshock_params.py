import os
from pathlib import Path
from utils.boardconf import BoardConf, from_pio_env, validate, print_header, print_footer
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

config_size = 0x3000

def update_partitions_file(partitions_file: Path):
    new_partitions_file = partitions_file.parent / 'partitions_modified.csv'
    partitions_file_path = str(partitions_file)
    updated_partitions = []
    spiffs_found = False

    with open(partitions_file_path, 'r') as file:
        reader = csv.reader(file, skipinitialspace=True)
        for row in reader:
            if row and row[0].lower() in ['spiffs', 'littlefs']:
                spiffs_found = True

                fs_name, fs_part_type, fs_part_subtype, fs_offset, fs_size, fs_flags = row
                # Making config partition to be added before existing FS partition
                config_partition = ['config', 'data', 'spiffs', fs_offset, hex(config_size)]
                # Forcing FS label to be 'littlefs'
                updated_fs_partition = ['littlefs', fs_part_type, fs_part_subtype, hex(int(fs_offset, 16) + config_size), hex(int(fs_size, 16) - config_size)]

                # Config partition should be specified before FS partition
                # Because PlatformIO uploads to the last partition with 'spiffs' subtype
                updated_partitions.append(config_partition)
                updated_partitions.append(updated_fs_partition)
            else:
                updated_partitions.append(row)

    if spiffs_found:
        with open(str(new_partitions_file), 'w', newline='') as file:
            writer = csv.writer(file)
            writer.writerows(updated_partitions)
        return str(new_partitions_file)
    else:
        print('WARNING: SPIFFS/LittleFS PARTITION NOT FOUND.')
        print('Not overriding default value.')
        return str(partitions_file)

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
    new_partitions_file = update_partitions_file(boardconf.get_partitions_file())
    board_config.update('build.partitions', new_partitions_file)


print_header()
use_openshock_params()
print_footer()
