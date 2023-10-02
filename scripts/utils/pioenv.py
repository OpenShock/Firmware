import conv

Import('env')  # type: ignore


# def __parse_pio_file():
#     leftover_flags = []
#     for flag in build_flags:
#         if flag.startswith('-D'):
#             flag = flag[2::]
#             if '=' in flag:
#                 (k, v) = flag.split('=', 1)

#                 cpp_defines[k] = v
#             else:
#                 cpp_defines[flag] = True
#         else:
#             leftover_flags.append(flag)


def get(key: str):
    return env[key]  # type: ignore


def set(key: str, value):
    env[key] = value  # type: ignore


def get_bool(key: str, default: bool | None = None) -> bool:
    try:
        return conv.to_bool(get(key))
    except Exception as ex:
        if default != None:
            return default
        raise ValueError('Failed to get pio config boolean: %s' % key)


# Get a string from the pio env, falling back to the default if provided.
def get_string(key: str, default: str | None = None) -> str:
    try:
        return conv.to_string(key)
    except Exception as ex:
        if default != None:
            return default
        raise ValueError('Failed to get pio string: %s' % key) from ex


# Get a string array from the pio env.
def get_string_array(key: str, default: list[str] | None = None) -> list[str]:
    strings = get(key) or default
    if strings == None or not isinstance(strings, list):
        raise ValueError('Failed to get pio string array: %s' % key)
    if any([not isinstance(v, str) for v in strings]):
        raise ValueError('Failed to get pio string array (found non-string values): %s' % key)
    return strings
