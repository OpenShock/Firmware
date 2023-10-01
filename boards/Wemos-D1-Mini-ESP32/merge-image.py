#!/bin/python3

import esptool

esptool.main([
    '--chip', 'esp32',
    'merge_bin', '-o', 'merged.bin',
    '0x1000', './bootloader.bin',
    '0x8000', './partitions.bin',
    '0x10000', './firmware.bin',
    '0x310000', './littlefs.bin'
])
