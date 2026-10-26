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
from typing import Any

from . import targets
from .espimage import DESC_BOARD_LEN
from .paths import ROOT

# The pin keys, in the order they sit in the descriptor.
PIN_KEYS = ('OPENSHOCK_RF_TX_GPIO', 'OPENSHOCK_ESTOP_PIN', 'OPENSHOCK_LED_GPIO', 'OPENSHOCK_LED_WS2812B')
BOOL_KEYS = ('OPENSHOCK_LED_SWAP_RG_CHANNELS',)
LIST_KEYS = ('OPENSHOCK_GPIO_BYPASS',)
KNOWN_KEYS = PIN_KEYS + BOOL_KEYS + LIST_KEYS

NO_PIN = -1
MAX_GPIO = 127
MAX_BYPASS_GPIO = 63

# The longest board name the image descriptor holds, less its NUL terminator.
NAME_MAX_LEN = DESC_BOARD_LEN - 1

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
        avail = ', '.join(all_boards())
        raise SystemExit(f'error: unknown board {board!r}\n  available: {avail}')
    return fragment


def parse_board_list(text: str) -> list[str]:
    """The board names in a list separated by whitespace, newlines or commas, in order.

    The one parser for every hop a board list takes: the dispatch input, get_vars' board-list
    output, the publish job's BOARDS, and the --board options of the packaging scripts.
    """
    return text.replace(',', ' ').split()


def board_names(values: list[str]) -> list[str]:
    """The board names a repeatable --board option holds, each value a board list (see parse_board_list).

    Refuses an empty list and a board named twice, so a CLI never packages or merges one twice.
    """
    names = [name for value in values for name in parse_board_list(value)]
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
    return targets.target(target).chip


def _parse_pin(fragment: Path, lineno: int, key: str, value: str) -> int:
    try:
        pin = int(value)
    except ValueError:
        raise SystemExit(f'error: {fragment}:{lineno}: {key} expects an integer GPIO, got {value!r}') from None
    if pin < NO_PIN or pin > MAX_GPIO:
        raise SystemExit(f'error: {fragment}:{lineno}: {key} must be -1 (no pin) or a GPIO number, got {pin}')
    return pin


def _parse_bool(fragment: Path, lineno: int, key: str, value: str) -> bool:
    if value not in ('y', 'n'):
        raise SystemExit(f'error: {fragment}:{lineno}: {key} is a boolean, expected y or n, got {value!r}')
    return value == 'y'


def _parse_pin_list(fragment: Path, lineno: int, key: str, value: str) -> tuple[int, ...]:
    pins = []
    for item in filter(None, (s.strip() for s in value.split(','))):
        pin = _parse_pin(fragment, lineno, key, item)
        if pin == NO_PIN or pin > MAX_BYPASS_GPIO:
            raise SystemExit(
                f'error: {fragment}:{lineno}: {key} entries must be GPIO numbers 0-{MAX_BYPASS_GPIO}, got {pin}'
            )
        pins.append(pin)
    if len(set(pins)) != len(pins):
        raise SystemExit(f'error: {fragment}:{lineno}: {key} lists a pin twice')
    return tuple(sorted(pins))


def _check_name(fragment: Path) -> str:
    name = fragment.stem
    if not name.isascii() or len(name) > NAME_MAX_LEN:
        raise SystemExit(
            f'error: {fragment}: a board name must be at most {NAME_MAX_LEN} ASCII characters to fit the descriptor, got {name!r}'
        )
    return name


def read_board_fragment(fragment: Path) -> BoardSpec:
    """Parse a fragment's ``OPENSHOCK_*=`` assignments and IDF target. Anything unrecognised is an error, so a typo cannot pass silently."""
    values: dict[str, Any] = {key: NO_PIN for key in PIN_KEYS}
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
            values[key] = _parse_bool(fragment, lineno, key, value)
        elif key in LIST_KEYS:
            values[key] = _parse_pin_list(fragment, lineno, key, value)
        else:
            values[key] = _parse_pin(fragment, lineno, key, value)

    name = _check_name(fragment)
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


# A variant is one compile shared by boards that differ only in the OPENSHOCK_* lines and flash size.
# It compiles at 4 MB, with partitions/ota_4mb.csv (the table sdkconfig.defaults names): the smallest app slot
# any board has, so an app that fits the variant fits every board. The partition table and the bootloader's
# flash-size header are per board, not per variant; scripts/package_boards.py writes both for each board.
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


def _is_partition_table_line(line: str) -> bool:
    # The table follows from the flash size (scripts/utils/partitions.py), so it never splits a variant either.
    return re.match(r'(#\s*)?CONFIG_PARTITION_TABLE_', line) is not None


def variant_kconfig(fragment: Path) -> list[str]:
    """The Kconfig lines that decide what a board compiles to: all of them but the flash size and partition table."""
    return [
        line for line in kconfig_lines(fragment) if not _is_flash_size_line(line) and not _is_partition_table_line(line)
    ]


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
