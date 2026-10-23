#!/usr/bin/env python3
"""Stamp, read or verify the OpenShock board descriptor in an ESP-IDF app image.

`stamp` writes boards/<board>.defaults into the image's descriptor (scripts/utils/stamping.py) in place
and recomputes its checksum and SHA-256. It refuses an image compiled without a descriptor or for
another chip.

    stamp_board.py stamp  --image build/firmware.bin --board Wemos-D1-Mini-ESP32
    stamp_board.py read   --image build/firmware.bin
    stamp_board.py verify --image build/firmware.bin --board Wemos-D1-Mini-ESP32
"""

import argparse
import dataclasses
import json

from utils import boards, espimage
from utils.stamping import read_descriptor, stamp_image, verify_image


def cmd_stamp(args) -> int:
    spec = boards.read_board(args.board)
    stamped = stamp_image(espimage.load(args.image), spec, args.image)
    with open(args.image, 'wb') as f:
        f.write(stamped.data)
    print(f'stamped {args.image} as {spec.name} ({spec.chip})')
    return 0


def cmd_read(args) -> int:
    image = espimage.load(args.image)
    image.verify()
    desc = read_descriptor(image, args.image)
    print(json.dumps({**dataclasses.asdict(desc), 'chip_id': image.chip_id, 'stamped': bool(desc.board)}, indent=2))
    return 0


def cmd_verify(args) -> int:
    spec = boards.read_board(args.board)
    verify_image(espimage.load(args.image), spec, args.image)
    print(f'{args.image}: descriptor matches {spec.name} ({spec.chip})')
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='command', required=True)

    p = sub.add_parser('stamp', help='write a board descriptor into an image, in place')
    p.add_argument('--image', required=True, help='app image to stamp (firmware.bin / app.bin)')
    p.add_argument('--board', required=True, help='board name (boards/<board>.defaults)')
    p.set_defaults(func=cmd_stamp)

    p = sub.add_parser('read', help='print the descriptor an image carries')
    p.add_argument('--image', required=True)
    p.set_defaults(func=cmd_read)

    p = sub.add_parser('verify', help='fail unless the image carries exactly this board\'s descriptor')
    p.add_argument('--image', required=True)
    p.add_argument('--board', required=True, help='board name (boards/<board>.defaults)')
    p.set_defaults(func=cmd_verify)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == '__main__':
    raise SystemExit(main())
