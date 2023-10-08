#!/bin/python3

import os
import sys
from pathlib import Path
from configparser import ConfigParser


def call_script(file):
    print('Calling script: %s' % file)
    print()
    with open(file) as f:
        exec(f.read())
    sys.exit(0)


# Read envs from PlatformIO config
config = ConfigParser()
config.read('platformio.ini')

# Get pio env name from CLI args
env_name = sys.argv[1]
env = config['env:' + env_name]

# Check if the board is custom.
board_name = env['board']
chip = env.get('custom_openshock.chip')
chip_variant = env.get('custom_openshock.chip_variant')

print('Board: %s' % board_name)
print('Chip: %s' % chip)
print('Chip variant: %s' % chip_variant)

# Find the directory with partitions.csv and merge-image.py
chips_dir = Path().absolute() / 'chips'
chip_base_dir = chips_dir / chip
chip_variant_dir = chip_base_dir
if chip_variant != None:
    chip_variant_dir = chip_base_dir / chip_variant

# Chip variant file paths
partitions_file = chip_variant_dir / 'partitions.csv'
merge_file = chip_variant_dir / 'merge-image.py'

chip_dir_exists = os.path.exists(chip_variant_dir)

print('Chip base dir: %s' % chip_base_dir)
print('Chip variant dir: %s' % chip_variant_dir)
print('Directory exists: %s' % chip_dir_exists)

# If the board exists directly, call `merge-image.py` in that directory.
if not chip_dir_exists:
    print('Directory does not exist: %s' % chip_variant_dir)
    print('FAILED TO FIND merge-image.py')
    sys.exit(1)

# Call the chips/{chip}/merge-image.py script
call_script(merge_file)
