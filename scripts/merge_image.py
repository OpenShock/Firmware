#!/bin/python3

import re
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
boards_dir = Path().absolute() / "boards"
board_exists_direct = os.path.exists(boards_dir / board_name / 'merge-image.py')

print('Board name: %s' % board_name)
print('Board found directly: %s' % board_exists_direct)

# If the board exists directly, call `merge-image.py` in that directory.
if board_exists_direct:
    print('Board exists directly!')
    call_script(boards_dir / board_name / 'merge-image.py')

# If the board name is NOT an indirect reference, fail.
if not board_name.startswith('OpenShock-'):
    print('Board not found directly: %s' % board_name)
    print('Board name does not start with "OpenShock-", so it\'s not an indirect name either.')
    print('FAILED TO FIND BOARD.')
    sys.exit(1)

# Grab the indirect name and check if it exists.
board_name_indirect = board_name[len('OpenShock-'):]
board_exists_indirect = os.path.exists(boards_dir / board_name_indirect / 'merge-image.py')
print('Board name indirectly: %s' % board_name_indirect)
print('Board found indirectly: %s' % board_exists_indirect)

if not board_exists_indirect:
    print('Board not found directly: %s' % board_name)
    print('Board not found indirectly: %s' % board_name_indirect)
    print('FAILED TO FIND BOARD.')
    sys.exit(1)

# The only option left is the board exists indirectly.
call_script(boards_dir / board_name_indirect / 'merge-image.py')
