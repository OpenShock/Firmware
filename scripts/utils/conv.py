BOOLEAN_MAP = {
    'true': True,
    'false': False,
    '1': True,
    '0': False,
}


def to_bool(value: str | None) -> bool:
    if value == None:
        return False
    value = value.lower()

    b = BOOLEAN_MAP.get(value)
    if b == None:
        raise ValueError('Value cannot be interpreted as boolean value: %s' % value)
    return b


def to_int(value: str | None) -> int:
    if value == None:
        return False
    return int(value)


def to_string(value: str | None) -> str:
    if value == None:
        raise ValueError('Value cannot be interpreted as string value: %s' % value)
    return value
