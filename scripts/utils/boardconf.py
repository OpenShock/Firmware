import os
from pathlib import Path
from configparser import ConfigParser

fallback_flash_size = '4MB'

class BoardConf:
    def __init__(self, chip: str, flash_size: str):
        self.chip = chip
        self.flash_size = flash_size
        if flash_size == '':
            self.flash_size = fallback_flash_size

        # Set dirs
        self.root_dir = Path().absolute()
        self.chips_dir = self.root_dir / 'chips'
        self.chip_base_dir = self.chips_dir / self.chip
        self.flash_size_dir = (
            (self.chip_base_dir / self.flash_size) if self.flash_size != '' else self.chip_base_dir
        )

    def get_chip(self) -> str:
        return self.chip

    def get_flash_size(self) -> str:
        return self.flash_size

    def get_chips_dir(self) -> Path:
        return self.chips_dir

    def get_chip_dir(self) -> Path:
        return self.chip_base_dir

    def get_flash_size_dir(self) -> Path:
        return self.flash_size_dir

    def override_flash_size(self, flash_size: str):
        self.flash_size = flash_size
        self.flash_size_dir = self.chip_base_dir / self.flash_size

    def get_merge_script(self) -> Path:
        return self.flash_size_dir / 'merge-image.py'

    def get_partitions_files(self, size: str) -> tuple[Path, Path]:
        return self.chips_dir / f'partitions_{size}.csv', self.chips_dir / f'partitions_{size}_OTA.csv', 

    def is_chip_specified(self) -> bool:
        return self.chip != ''

    def is_flash_size_specified(self) -> bool:
        return self.flash_size != ''

    # Whether the chip folder exists.
    def does_chip_exist(self) -> bool:
        return os.path.exists(self.get_chip_dir())

    # Whether the flash size folder exists.
    def does_flash_size_exist(self, size: str = "") -> bool:
        if size == "":
            return os.path.exists(self.get_flash_size_dir())
        else:
            return os.path.exists(self.chip_base_dir / size)

    # Whether the merge script for the current flash size exists.
    def does_merge_script_exist(self) -> bool:
        return os.path.exists(self.get_merge_script())

    # Whether the partitions file for the current flash size exists.
    def do_partitions_files_exists(self, size: str) -> bool:
        partitions, partitions_ota = self.get_partitions_files(size)
        return os.path.exists(partitions) and os.path.exists(partitions_ota)


def from_pio_file(env_name: str) -> BoardConf:
    config = ConfigParser()
    config.read('platformio.ini')

    # Grab env
    env = config['env:' + env_name]

    # Grab strings from platformio.ini env declaration
    # TODO: Simplify platformio.ini to only have one env (Flash Size)
    # Need to either pass in or figure out chip
    chip = env.get('custom_openshock.chip', '')
    flash_size = env.get('custom_openshock.flash_size', '')

    return BoardConf(chip, flash_size)


def from_pio_env(env):
    # Grab strings from current pio env
    # TODO: Simplify platformio.ini to only have one env (Flash Size)
    # env.BoardConfig().get('build.mcu')
    # can be used to get chip
    chip = env.GetProjectOption('custom_openshock.chip', '')
    flash_size = env.GetProjectOption('custom_openshock.flash_size', '')

    return BoardConf(chip, flash_size)


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
        print('Skipping further chip/flash size-specific initialization.')
        return False

    # Print info about determined chip/flash size.
    print('Chip Variant: %s' % boardconf.get_chip())
    print('Flash Size: %s' % boardconf.get_flash_size())
    print('')

    # Handle chip not existing.
    if not boardconf.does_chip_exist():
        print('Chip directory does not exist: %s' % boardconf.get_chip_dir())
        print('Did you make a typo?')
        return False

    # Handle specified flash size not existing.
    if not boardconf.does_flash_size_exist():
        print('Flash size directory does not exist: %s' % boardconf.get_flash_size_dir())
        # Attempt to try fallback flash size.
        if boardconf.does_flash_size_exist(fallback_flash_size):
            print(f'Falling back to {fallback_flash_size} flash size.')
            boardconf.override_flash_size(fallback_flash_size)
        else:
            print(f'Failed to fallback to {fallback_flash_size} flash size.')
            print('Did you make a typo?')
            return False

    # Print expected location of merge script and partitions file
    print('Determined merge script: %s' % boardconf.get_merge_script())
    print('Potential partition files: %s' % str(boardconf.get_partitions_files(boardconf.get_flash_size())))
    print('')

    return True
