#!/usr/bin/env python3
"""Stamp, read or verify the OpenShock board descriptor in an ESP-IDF app image.

The firmware is compiled per chip and carries a 256-byte board descriptor
(BoardDescriptor.h) at a fixed offset of the app image, right after ESP-IDF's own app
descriptor. `stamp` writes a board's description (from boards/<board>.defaults) into a
copy of the image and fixes up the image checksum and SHA-256, so one compile serves
every board on that chip. The device reads the descriptor at boot for its pins and its
OTA identity, and OTA refuses an image whose descriptor names another board.

    stamp_board.py stamp  --image build/firmware.bin --board Wemos-D1-Mini-ESP32 --output out.bin
    stamp_board.py read   --image out.bin
    stamp_board.py verify --image out.bin --board Wemos-D1-Mini-ESP32

`stamp` refuses an image that was not compiled with a descriptor (there is nothing to
replace, and writing into arbitrary rodata would corrupt the firmware) and an image built
for another chip than the board uses.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from utils import boards, espimage  # noqa: E402


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


def load_spec(args) -> boards.BoardSpec:
    if args.fragment:
        return boards.read_board_fragment(Path(args.fragment), args.board)
    return boards.read_board(args.board)


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


def cmd_stamp(args) -> int:
    spec = load_spec(args)
    image = espimage.load(args.image)
    image.verify()
    check_chip(image, spec)
    try:
        current = espimage.unpack_descriptor(image.descriptor_bytes())
    except espimage.DescriptorError as err:
        raise SystemExit(
            f'error: {args.image} carries no board descriptor at 0x{espimage.DESC_OFFSET:X} ({err}); was it built with BoardDescriptor.cpp?'
        )

    stamped = image.patch(espimage.DESC_OFFSET, descriptor_for(spec).pack())
    output = Path(args.output or args.image)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(stamped.data)
    print(f'stamped {output} as {spec.name} ({spec.chip}); was {current.board}')
    return 0


def cmd_read(args) -> int:
    image = espimage.load(args.image)
    image.verify()
    try:
        desc = espimage.unpack_descriptor(image.descriptor_bytes())
    except espimage.DescriptorError as err:
        raise SystemExit(f'error: {args.image} carries no valid board descriptor ({err})')
    print(json.dumps({**desc.as_dict(), 'chip_id': image.chip_id}, indent=2))
    return 0


def cmd_verify(args) -> int:
    spec = load_spec(args)
    image = espimage.load(args.image)
    image.verify()
    check_chip(image, spec)
    try:
        desc = espimage.unpack_descriptor(image.descriptor_bytes())
    except espimage.DescriptorError as err:
        raise SystemExit(f'error: {args.image} carries no valid board descriptor ({err})')
    expected = descriptor_for(spec)
    if desc != expected:
        diff = {k: (v, expected.as_dict()[k]) for k, v in desc.as_dict().items() if v != expected.as_dict()[k]}
        raise SystemExit(f'error: descriptor in {args.image} does not match {spec.name}: {diff}')
    print(f'{args.image}: descriptor matches {spec.name} ({spec.chip})')
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)

    def board_args(p):
        p.add_argument('--board', required=True, help='board name (boards/<board>.defaults)')
        p.add_argument('--fragment', help='read the board from this fragment instead of boards/<board>.defaults')

    p = sub.add_parser('stamp', help='write a board descriptor into an image')
    p.add_argument('--image', required=True, help='app image to stamp (firmware.bin / app.bin)')
    board_args(p)
    p.add_argument('--output', help='where to write the stamped image; defaults to in place')
    p.set_defaults(func=cmd_stamp)

    p = sub.add_parser('read', help='print the descriptor an image carries')
    p.add_argument('--image', required=True)
    p.set_defaults(func=cmd_read)

    p = sub.add_parser('verify', help='fail unless the image carries exactly this board\'s descriptor')
    p.add_argument('--image', required=True)
    board_args(p)
    p.set_defaults(func=cmd_verify)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == '__main__':
    raise SystemExit(main())
