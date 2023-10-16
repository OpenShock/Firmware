import os
from pathlib import Path
from configparser import ConfigParser


class BoardConf:
    def __init__(self, chip: str, chip_variant: str):
        self.chip = chip
        self.chip_variant = chip_variant

        # Set dirs
        self.root_dir = Path().absolute()
        self.chips_dir = self.root_dir / 'chips'
        self.chip_base_dir = self.chips_dir / self.chip
        self.chip_variant_dir = (
            (self.chip_base_dir / self.chip_variant) if self.chip_variant != '' else self.chip_base_dir
        )

    def get_chip(self) -> str:
        return self.chip

    def get_chip_variant(self) -> str:
        return self.chip_variant

    def get_chips_dir(self) -> Path:
        return self.chips_dir

    def get_chip_dir(self) -> Path:
        return self.chip_base_dir

    def get_chip_variant_dir(self) -> Path:
        return self.chip_variant_dir

    def get_merge_script(self) -> Path:
        return self.chip_variant_dir / 'merge-image.py'

    def get_partitions_file(self) -> Path:
        return self.chip_variant_dir / 'partitions.csv'

    def is_chip_specified(self) -> bool:
        return self.chip != ''

    def is_chip_variant_specified(self) -> bool:
        return self.chip_variant != ''

    # Whether the chip folder exists.
    def does_chip_exist(self) -> bool:
        return os.path.exists(self.get_chip_dir())

    # Whether the chip variant folder exists.
    def does_chip_variant_exist(self) -> bool:
        return os.path.exists(self.get_chip_variant_dir())

    # Whether the merge script for the current chip variant exists.
    def does_merge_script_exist(self) -> bool:
        return os.path.exists(self.get_merge_script())

    # Whether the partitiosn file for the current chip variant exists.
    def does_partitions_file_exists(self) -> bool:
        return os.path.exists(self.get_partitions_file())


def from_pio_file(env_name: str) -> BoardConf:
    config = ConfigParser()
    config.read('platformio.ini')

    # Grab env
    env = config['env:' + env_name]

    # Grab strings from platformio.ini env declaration
    chip = env.get('custom_openshock.chip', '')
    chip_variant = env.get('custom_openshock.chip_variant', '')

    return BoardConf(chip, chip_variant)


def from_pio_env(env):
    # Grab strings from current pio env
    chip = env.GetProjectOption('custom_openshock.chip', '')
    chip_variant = env.GetProjectOption('custom_openshock.chip_variant', '')

    return BoardConf(chip, chip_variant)


def print_header():
    print('========================')
    print('       OpenShock        ')
    print('========================')
    print('')


def print_footer():
    print('========================')
    print('')


def validate(boardconf) -> bool:
    # Handle missing chip declaration.
    if not boardconf.is_chip_specified():
        print('No chip specified.')
        print('Skipping further chip/chip-variant specific initialization.')
        return False

    # Print info about determined chip/variant.
    print('Chip: %s' % boardconf.get_chip())
    print('Variant: %s' % boardconf.get_chip_variant())
    print('')

    # Handle chip not existing.
    if not boardconf.does_chip_exist():
        print('Chip directory does not exist: %s' % boardconf.get_chip_dir())
        print('Did you make a typo?')
        return False

    # Handle specified variant not existing.
    if not boardconf.does_chip_variant_exist():
        print('Chip VARIANT directory does not exist: %s' % boardconf.get_chip_variant_dir())
        print('Did you make a typo?')
        return False

    # Print expected location of merge script and partitions file
    print('Determined merge script: %s' % boardconf.get_merge_script())
    print('Determined partitions file: %s' % boardconf.get_partitions_file())
    print('')

    return True
