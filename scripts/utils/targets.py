"""The ESP-IDF targets the tooling knows, and what each one is called in the firmware and in an app image.

One entry per target, so adding a chip is one line: the chip label the firmware reports
(OPENSHOCK_FW_CHIP, the board descriptor's chip field) and the image header's esp_chip_id_t.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Target:
    chip: str  # chip label, e.g. ESP32-S3
    image_chip_id: int  # esp_chip_id_t, at offset 0x0C of an app image header


TARGETS = {
    'esp32': Target('ESP32', 0x0000),
    'esp32s2': Target('ESP32-S2', 0x0002),
    'esp32c3': Target('ESP32-C3', 0x0005),
    'esp32s3': Target('ESP32-S3', 0x0009),
    'esp32c2': Target('ESP32-C2', 0x000C),
    'esp32c6': Target('ESP32-C6', 0x000D),
    'esp32h2': Target('ESP32-H2', 0x0010),
}


def target(name: str) -> Target:
    found = TARGETS.get(name)
    if found is None:
        raise SystemExit(f'error: unknown IDF target {name!r} - add it to scripts/utils/targets.py')
    return found
