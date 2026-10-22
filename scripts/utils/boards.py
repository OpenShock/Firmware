"""Read a board fragment (boards/<name>.defaults) into a BoardSpec.

A fragment holds two kinds of line. ``CONFIG_*`` lines are real Kconfig, layered onto
sdkconfig.defaults by scripts/build.py. Bare ``OPENSHOCK_*`` lines are the board's
hardware description - pins, LED flags, the chip-unsafe pins the board is allowed to use
anyway - and are deliberately not Kconfig (see scripts/build.py). This module is the one
parser for those lines, shared by gen_env_header.py (the compiled-in defaults),
stamp_board.py (the descriptor written into the app image) and merge_image.py.
"""

import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

# IDF target -> the chip label the firmware reports (OPENSHOCK_FW_CHIP, the descriptor's chip field).
TARGET_TO_CHIP = {
    'esp32': 'ESP32',
    'esp32s2': 'ESP32-S2',
    'esp32s3': 'ESP32-S3',
    'esp32c3': 'ESP32-C3',
    'esp32c6': 'ESP32-C6',
}

# The pin keys, in the order they sit in the descriptor.
PIN_KEYS = ('OPENSHOCK_RF_TX_GPIO', 'OPENSHOCK_ESTOP_PIN', 'OPENSHOCK_LED_GPIO', 'OPENSHOCK_LED_WS2812B')
BOOL_KEYS = ('OPENSHOCK_LED_SWAP_RG_CHANNELS',)
LIST_KEYS = ('OPENSHOCK_GPIO_BYPASS',)
KNOWN_KEYS = PIN_KEYS + BOOL_KEYS + LIST_KEYS

NO_PIN = -1

# Descriptor flag bits (BoardDescriptor.h: OPENSHOCK_BOARD_FLAG_*).
FLAG_LED_SWAP_RG = 1 << 0


@dataclass(frozen=True)
class BoardSpec:
    name: str
    target: str  # IDF target, e.g. esp32s3
    chip: str  # chip label, e.g. ESP32-S3
    rf_tx_gpio: int = NO_PIN
    estop_gpio: int = NO_PIN
    led_gpio: int = NO_PIN
    ws2812b_gpio: int = NO_PIN
    led_swap_rg: bool = False
    gpio_bypass: tuple[int, ...] = ()

    @property
    def flags(self) -> int:
        return FLAG_LED_SWAP_RG if self.led_swap_rg else 0

    @property
    def gpio_bypass_mask(self) -> int:
        mask = 0
        for pin in self.gpio_bypass:
            mask |= 1 << pin
        return mask

    @property
    def pins(self) -> dict[str, int]:
        return {
            'OPENSHOCK_RF_TX_GPIO': self.rf_tx_gpio,
            'OPENSHOCK_ESTOP_PIN': self.estop_gpio,
            'OPENSHOCK_LED_GPIO': self.led_gpio,
            'OPENSHOCK_LED_WS2812B': self.ws2812b_gpio,
        }


def board_fragment(board: str) -> Path:
    fragment = ROOT / 'boards' / f'{board}.defaults'
    if not fragment.is_file():
        avail = ', '.join(sorted(p.stem for p in (ROOT / 'boards').glob('*.defaults')))
        raise SystemExit(f'error: unknown board {board!r}\n  available: {avail}')
    return fragment


def read_target(fragment: Path) -> str:
    for line in fragment.read_text(encoding='utf-8').splitlines():
        m = re.match(r'\s*CONFIG_IDF_TARGET\s*=\s*"([^"]+)"', line)
        if m:
            return m.group(1)
    raise SystemExit(f'error: {fragment} does not set CONFIG_IDF_TARGET')


def chip_for_target(target: str) -> str:
    chip = TARGET_TO_CHIP.get(target)
    if chip is None:
        raise SystemExit(f'error: no chip label known for IDF target {target!r} - add it to scripts/utils/boards.py')
    return chip


def _parse_pin(fragment: Path, lineno: int, key: str, value: str) -> int:
    try:
        pin = int(value)
    except ValueError:
        raise SystemExit(f'error: {fragment}:{lineno}: {key} expects an integer GPIO, got {value!r}')
    if pin < NO_PIN or pin > 127:
        raise SystemExit(f'error: {fragment}:{lineno}: {key} must be -1 (no pin) or a GPIO number, got {pin}')
    return pin


def read_board_fragment(fragment: Path, name: str | None = None) -> BoardSpec:
    """Parse the bare ``OPENSHOCK_*=`` assignments and the IDF target out of a fragment.

    Anything unrecognised is an error rather than a silent no-op: a typo in a pin name
    would otherwise leave the board on the default and be very hard to spot.
    """
    if not fragment.is_file():
        raise SystemExit(f'error: board fragment {fragment} not found')

    values: dict[str, object] = {key: NO_PIN for key in PIN_KEYS}
    values.update({key: False for key in BOOL_KEYS})
    values.update({key: () for key in LIST_KEYS})

    for lineno, raw in enumerate(fragment.read_text(encoding='utf-8').splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#') or line.startswith('CONFIG_'):
            continue
        if '=' not in line:
            raise SystemExit(f'error: {fragment}:{lineno}: expected KEY=VALUE, got {line!r}')
        key, _, value = line.partition('=')
        key, value = key.strip(), value.strip()
        if key not in KNOWN_KEYS:
            raise SystemExit(
                f'error: {fragment}:{lineno}: unknown board setting {key!r}\n  known: {", ".join(KNOWN_KEYS)}'
            )

        if key in BOOL_KEYS:
            if value not in ('y', 'n'):
                raise SystemExit(f'error: {fragment}:{lineno}: {key} is a boolean, expected y or n, got {value!r}')
            values[key] = value == 'y'
        elif key in LIST_KEYS:
            pins = []
            for item in filter(None, (s.strip() for s in value.split(','))):
                pin = _parse_pin(fragment, lineno, key, item)
                if pin == NO_PIN or pin > 63:
                    raise SystemExit(f'error: {fragment}:{lineno}: {key} entries must be GPIO numbers 0-63, got {pin}')
                pins.append(pin)
            if len(set(pins)) != len(pins):
                raise SystemExit(f'error: {fragment}:{lineno}: {key} lists a pin twice')
            values[key] = tuple(sorted(pins))
        else:
            values[key] = _parse_pin(fragment, lineno, key, value)

    target = read_target(fragment)
    return BoardSpec(
        name=name or fragment.stem,
        target=target,
        chip=chip_for_target(target),
        rf_tx_gpio=values['OPENSHOCK_RF_TX_GPIO'],
        estop_gpio=values['OPENSHOCK_ESTOP_PIN'],
        led_gpio=values['OPENSHOCK_LED_GPIO'],
        ws2812b_gpio=values['OPENSHOCK_LED_WS2812B'],
        led_swap_rg=values['OPENSHOCK_LED_SWAP_RG_CHANNELS'],
        gpio_bypass=values['OPENSHOCK_GPIO_BYPASS'],
    )


def read_board(board: str) -> BoardSpec:
    return read_board_fragment(board_fragment(board), board)
