import os
from pathlib import Path

#
# This file is responsible for processing the "custom_openshock.*"
# parameters declared in `platformio.ini`.
#

Import('env')  # type: ignore
env = env  # type: ignore

# Read OpenShock variables.
# Docs:
#     https://docs.platformio.org/en/latest/scripting/examples/platformio_ini_custom_options.html
# "custom_" prefix reason:
#     https://community.platformio.org/t/custom-build-target-configuration-option-warnings/26251
chip = env.GetProjectOption('custom_openshock.chip', '')
chip_variant = env.GetProjectOption('custom_openshock.chip_variant', '')

# Determine chip directory
chips_dir = Path().absolute() / 'chips'
chip_base_dir = chips_dir / chip
chip_variant_dir = chip_base_dir
if chip_variant != None:
    chip_variant_dir = chip_base_dir / chip_variant

# Chip variant file paths
partitions_file = chip_variant_dir / 'partitions.csv'
merge_file = chip_variant_dir / 'merge-image.py'

# Start pretty printing to inform the user (and us) what's going on :^)
print('========================')
print('       OpenShock        ')
print('========================')
print('')

print('Chip: %s' % chip)
print('Variant: %s' % chip_variant)
print('')

print('Chip directory: %s' % chip_variant_dir)
print('Partitions file: %s' % partitions_file)
print('Merge script: %s' % merge_file)

print('')
print('========================')
print('')

# Set partition file to chip dir.
# https://docs.platformio.org/en/latest/scripting/examples/override_board_configuration.html
board_config = env.BoardConfig()
board_config.update('build.partitions', str(partitions_file))
