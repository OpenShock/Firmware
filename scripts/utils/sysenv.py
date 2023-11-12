import os
from . import conv


def get_bool(key: str, default: bool | None = None) -> bool:
    try:
        return conv.to_bool(os.environ.get(key))
    except Exception as ex:
        if default != None:
            return default
        raise ValueError('Failed to get environment boolean: %s' % key) from ex


def get_string(key: str, default: str | None = None) -> str:
    try:
        return conv.to_string(os.environ.get(key))
    except Exception as ex:
        if default != None:
            return default
        raise ValueError('Failed to get environment string: %s' % key) from ex


def get_all_prefixed(prefix: str) -> dict[str, str]:
    result = {}
    for key, value in os.environ.items():
        if key.startswith(prefix):
            result[key] = value
    return result
