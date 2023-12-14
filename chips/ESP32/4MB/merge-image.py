#!/bin/python3

import esptool

# fmt: off
esptool.main([
    '--chip', 'esp32',
    'merge_bin', '-o', 'merged.bin',
    '--flash_size', '4MB',
    '0x1000', './bootloader.bin',
    '0x8000', './partitions.bin',
    '0x10000', './app.bin',
    '0x353000', './staticfs.bin'
])
# fmt: on
