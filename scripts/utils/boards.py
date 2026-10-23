"""Board fragments (boards/<name>.defaults) and the build variants they group into.

A fragment's ``CONFIG_*`` lines are Kconfig. Its bare ``OPENSHOCK_*`` lines are the board's pins and
LED flags, which are stamped into the image after linking rather than compiled in, so boards that
differ only in them can share one compile.
"""

import hashlib
import re
from collections.abc import Iterable
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

# The longest board name the image descriptor holds: espimage.DESC_BOARD_LEN less its NUL terminator.
NAME_MAX_LEN = 47

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
        return sum(1 << pin for pin in self.gpio_bypass)


def board_fragment(board: str) -> Path:
    fragment = ROOT / 'boards' / f'{board}.defaults'
    if not fragment.is_file():
        avail = ', '.join(sorted(p.stem for p in (ROOT / 'boards').glob('*.defaults')))
        raise SystemExit(f'error: unknown board {board!r}\n  available: {avail}')
    return fragment


def board_names(values: list[str]) -> list[str]:
    """The board names a repeatable --board option holds, each value split on whitespace.

    Refuses an empty list and a board named twice, so a CLI never packages or merges one twice.
    """
    names = [name for value in values for name in value.split()]
    if not names:
        raise SystemExit('error: --board matched no board names')
    repeated = sorted({name for name in names if names.count(name) > 1})
    if repeated:
        raise SystemExit(f'error: repeated board(s): {", ".join(repeated)}')
    return names


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


def read_board_fragment(fragment: Path) -> BoardSpec:
    """Parse a fragment's ``OPENSHOCK_*=`` assignments and IDF target. Anything unrecognised is an error, so a typo cannot pass silently."""
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

    name = fragment.stem
    if not name.isascii() or len(name) > NAME_MAX_LEN:
        raise SystemExit(
            f'error: {fragment}: a board name must be at most {NAME_MAX_LEN} ASCII characters to fit the descriptor, got {name!r}'
        )

    target = read_target(fragment)
    return BoardSpec(
        name=name,
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
    return read_board_fragment(board_fragment(board))


def all_boards() -> list[str]:
    return sorted(p.stem for p in (ROOT / 'boards').glob('*.defaults'))


# --- Flash size --------------------------------------------------------------------------

# ESP-IDF's own default when no sdkconfig input picks a CONFIG_ESPTOOLPY_FLASHSIZE_* choice.
DEFAULT_FLASH_SIZE = '2MB'


def parse_flash_size(text: str) -> str | None:
    """The flash size an sdkconfig fragment selects, e.g. '8MB', or None when it picks none.

    It is a Kconfig choice, so the selected option reads CONFIG_ESPTOOLPY_FLASHSIZE_<size>=y.
    The last selection wins, as it would when sdkconfig layers the fragment.
    """
    size = None
    for line in text.splitlines():
        m = re.match(r'\s*CONFIG_ESPTOOLPY_FLASHSIZE_(\d+MB)\s*=\s*y\s*$', line)
        if m:
            size = m.group(1)
    return size


def layered_sdkconfig(board: str) -> list[str]:
    """The text of every sdkconfig input a board build layers, in order: shared defaults, then the board."""
    shared = ROOT / 'sdkconfig.defaults'
    paths = [shared] if shared.is_file() else []
    return [path.read_text(encoding='utf-8') for path in (*paths, board_fragment(board))]


def sdkconfig_value(board: str, key: str) -> str | None:
    """The value of `key` the board build ends up with, quotes stripped; None when nothing sets it."""
    value = None
    for text in layered_sdkconfig(board):
        for line in text.splitlines():
            m = re.match(rf'\s*{re.escape(key)}\s*=\s*"?([^"]*)"?\s*$', line)
            if m:
                value = m.group(1)
    return value


def flash_size(board: str) -> str:
    """The board's flash size, layered the way scripts/build.py layers sdkconfig."""
    size = None
    for text in layered_sdkconfig(board):
        size = parse_flash_size(text) or size
    return size or DEFAULT_FLASH_SIZE


def flash_size_bytes(size: str) -> int:
    return int(size.removesuffix('MB')) * 1024 * 1024


# --- Build variants -------------------------------------------------------------------
# A variant is one compile, and every board whose fragment differs from it only in what
# is settled after linking. That is two things:
#
#   - the OPENSHOCK_* lines, stamped into the app image by utils/stamping.py;
#   - the flash size. It feeds the partition table's fit check and the image headers'
#     flash-size field, and nothing else: no board's partition table, and no firmware
#     code, uses flash past VARIANT_FLASH_SIZE, the size partitions/ota_4mb.csv is laid
#     out for. So a variant compiles at that size and every board ships its images as
#     they are; a bigger board's bootloader logs that the chip is larger than its header
#     says and uses the header's size. A board that wants its extra flash needs a bigger
#     partition table, which is Kconfig and so a variant of its own - and the fit check
#     fails that variant's build until it compiles at the board's size.
#
# Every other Kconfig line is compiled in and splits the variant: the S3 boards on the
# USB-Serial-JTAG console and the ones on the UART cannot share an image, because the
# serial driver and the bootloader's console are both chosen at compile time.
#
# Variants are derived from whichever boards a build asks for, never declared, so a board
# that changes a Kconfig setting moves into its own variant by itself.

VARIANT_FLASH_SIZE = '4MB'


def _is_kconfig_line(line: str) -> bool:
    # `# CONFIG_X is not set` is the one comment form that means something: it disables a bool.
    return line.startswith('CONFIG_') or (line.startswith('# CONFIG_') and line.endswith(' is not set'))


def _is_flash_size_line(line: str) -> bool:
    return re.match(r'(#\s*)?CONFIG_ESPTOOLPY_FLASHSIZE_', line) is not None


def kconfig_lines(fragment: Path) -> list[str]:
    """The Kconfig lines of an sdkconfig fragment, stripped, in file order.

    Comments, blank lines and the bare OPENSHOCK_* board assignments are dropped.
    """
    lines = []
    for raw in fragment.read_text(encoding='utf-8').splitlines():
        line = raw.strip()
        if _is_kconfig_line(line):
            lines.append(line)
    return lines


def variant_kconfig(fragment: Path) -> list[str]:
    """The Kconfig lines that decide what a board compiles to: all of them but the flash size."""
    return [line for line in kconfig_lines(fragment) if not _is_flash_size_line(line)]


def variant_id(fragment: Path) -> str:
    """Name the compile a board needs: its IDF target plus a digest of its compiled-in Kconfig.

    The order-independent digest makes two fragments listing the same settings in another
    order the same variant. sdkconfig.defaults is not part of it: every board shares it,
    so it cannot tell two variants apart, and leaving it out keeps the id - which also
    names the ccache store - stable across edits to the shared defaults.
    """
    digest = hashlib.sha256('\n'.join(sorted(variant_kconfig(fragment))).encode('utf-8')).hexdigest()
    return f'{read_target(fragment)}-{digest[:8]}'


@dataclass(frozen=True)
class Variant:
    id: str
    target: str
    # The compiled-in Kconfig, in the file order of the first board's fragment.
    kconfig: tuple[str, ...]
    boards: tuple[str, ...]

    def sdkconfig_fragment(self) -> str:
        """The fragment scripts/build.py layers onto sdkconfig.defaults for this variant."""
        return '\n'.join([*self.kconfig, f'CONFIG_ESPTOOLPY_FLASHSIZE_{VARIANT_FLASH_SIZE}=y']) + '\n'


def group_variants(names: Iterable[str]) -> list[Variant]:
    """The variants that build these boards, sorted by id; each lists its boards sorted."""
    members: dict[str, list[str]] = {}
    for name in sorted(set(names)):
        members.setdefault(variant_id(board_fragment(name)), []).append(name)

    variants = []
    for vid, names_in in sorted(members.items()):
        fragment = board_fragment(names_in[0])
        variants.append(
            Variant(
                id=vid,
                target=read_target(fragment),
                kconfig=tuple(variant_kconfig(fragment)),
                boards=tuple(names_in),
            )
        )
    return variants


def find_variant(vid: str) -> Variant:
    """The variant with this id among every board in the repository."""
    variants = group_variants(all_boards())
    for variant in variants:
        if variant.id == vid:
            return variant
    avail = '\n'.join(f'  {v.id}  {", ".join(v.boards)}' for v in variants)
    raise SystemExit(f'error: unknown build variant {vid!r}\n  available:\n{avail}')
