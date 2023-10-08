#!/bin/python3

import os
import sys
from pathlib import Path
from configparser import ConfigParser
from utils.boardconf import BoardConf, from_pio_file, validate, print_header, print_footer


def call_script(file):
    print('Calling script: %s' % file)
    print()
    with open(file) as f:
        exec(f.read())
    sys.exit(0)


# Get pio env name from CLI args
env_name = sys.argv[1]

# Read board configuration from platformio.ini
boardconf: BoardConf = from_pio_file(env_name)


def merge_image():
    if not validate(boardconf):
        return

    # If the board exists directly, call `merge-image.py` in that directory.
    if not boardconf.does_merge_script_exist():
        print('ERROR: MERGE SCRIPT DOES NOT EXIST: %s' % boardconf.get_merge_script())
        print('FAILED TO FIND merge-image.py')
        sys.exit(1)


print_header()
merge_image()
print_footer()


# Call the chips/{chip}/merge-image.py script
call_script(boardconf.get_merge_script())
