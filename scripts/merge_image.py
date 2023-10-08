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
chip_name = env.get('custom_openshock.chip')
chip_variant = env.get('custom_openshock.chip_variant')

print('Board name: %s' % board_name)
print('Chip name: %s' % chip_name)
print('Chip variant: %s' % chip_variant)

# Find the directory with partitions.csv and merge-image.py
chips_dir = Path().absolute() / 'chips'
chip_dir = chips_dir / chip_name
chip_dir_exists = os.path.exists(chip_dir)

print('Chip dir exists: %s' % chip_dir_exists)

# If the board exists directly, call `merge-image.py` in that directory.
if not chip_dir_exists:
    print('Chip dir does not exist: %s' % chip_dir)
    print('FAILED TO FIND merge-image.py')
    sys.exit(1)

# Call the chips/{chip}/merge-image.py script
call_script(chip_dir / 'merge-image.py')
