"""Stamp a board's descriptor into an app image, and check an image carries it.

The descriptor (BoardDescriptor.h, 256 bytes right after ESP-IDF's app descriptor) names the board and
its pins; the device reads it at boot, and OTA refuses an image stamped for another board. Used by
scripts/stamp_board.py (the CLI, and CMake's POST_BUILD stamp) and scripts/package_boards.py.
"""

import dataclasses

from . import boards, espimage


def descriptor_for(spec: boards.BoardSpec) -> espimage.Descriptor:
    return espimage.Descriptor(
        board=spec.name,
        chip=spec.chip,
        rf_tx_gpio=spec.rf_tx_gpio,
        estop_gpio=spec.estop_gpio,
        led_gpio=spec.led_gpio,
        ws2812b_gpio=spec.ws2812b_gpio,
        flags=spec.flags,
        gpio_bypass_mask=spec.gpio_bypass_mask,
    )


def check_chip(image: espimage.Image, spec: boards.BoardSpec) -> None:
    expected = espimage.CHIP_IDS.get(spec.target)
    if expected is None:
        raise SystemExit(
            f'error: no image chip id known for target {spec.target!r} - add it to scripts/utils/espimage.py'
        )
    if image.chip_id != expected:
        raise SystemExit(
            f'error: image was built for chip id 0x{image.chip_id:04X}, but {spec.name} is {spec.target} (0x{expected:04X})'
        )


def read_descriptor(image: espimage.Image, path) -> espimage.Descriptor:
    try:
        return espimage.unpack_descriptor(image.descriptor_bytes())
    except espimage.DescriptorError as err:
        raise SystemExit(
            f'error: {path} carries no board descriptor at 0x{espimage.DESC_OFFSET:X} ({err}); was it built with BoardDescriptor.cpp?'
        ) from err


def stamp_image(image: espimage.Image, spec: boards.BoardSpec, path) -> espimage.Image:
    """A copy of the image stamped as this board, its checksum and SHA-256 recomputed."""
    image.verify()
    check_chip(image, spec)
    read_descriptor(image, path)  # there must be one to replace: writing anywhere else would corrupt the image
    return image.patch(espimage.DESC_OFFSET, descriptor_for(spec).pack())


def verify_image(image: espimage.Image, spec: boards.BoardSpec, path) -> None:
    """Fail unless the image is intact, for the board's chip, and carries exactly its descriptor."""
    image.verify()
    check_chip(image, spec)
    desc = read_descriptor(image, path)
    expected = descriptor_for(spec)
    if desc != expected:
        want = dataclasses.asdict(expected)
        diff = {k: (v, want[k]) for k, v in dataclasses.asdict(desc).items() if v != want[k]}
        raise SystemExit(f'error: descriptor in {path} does not match {spec.name}: {diff}')
