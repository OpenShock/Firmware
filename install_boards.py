#!/bin/python3

import os
import shutil
from pathlib import Path

source_dir = Path().absolute() / "boards"
target_dir = Path.home() / ".platformio" / "boards"

def create_boards_dir():
    os.makedirs(target_dir, exist_ok=True)

def wipe_installed_boards():
    print('Wiping installed boards')
    for filename in os.listdir(target_dir):
        path = os.path.join(target_dir, filename)
        print('  Deleting: %s' % path)
        try:
            if os.path.isfile(path) or os.path.islink(path):
                os.unlink(path)
            elif os.path.isdir(path):
                shutil.rmtree(path)
        except Exception as e:
            print('Failed to delete boards. Reason: %s' % (path, e))

def install_board(file):
    dest = target_dir / ("OpenShock-" + file +  ".json")
    print("  Installing: %s" % dest)
    shutil.copyfile(source_dir / file / "board.json", dest)

def install_boards():
    print("Installing defined boards")
    for filename in os.listdir(source_dir):
        if os.path.exists(source_dir / filename / "board.json"):
            install_board(filename)

create_boards_dir()
wipe_installed_boards()
install_boards()
