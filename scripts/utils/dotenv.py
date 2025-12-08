from pathlib import Path
from typing import Mapping

LOGLEVEL_MAP: dict[str, tuple[int, str]] = {
    'none': (0, 'LOG_NONE'),
    'log_none': (0, 'LOG_NONE'),
    'error': (1, 'LOG_ERROR'),
    'log_error': (1, 'LOG_ERROR'),
    'warn': (2, 'LOG_WARN'),
    'log_warn': (2, 'LOG_WARN'),
    'warning': (2, 'LOG_WARN'),
    'log_warning': (2, 'LOG_WARN'),
    'info': (3, 'LOG_INFO'),
    'log_info': (3, 'LOG_INFO'),
    'debug': (4, 'LOG_DEBUG'),
    'log_debug': (4, 'LOG_DEBUG'),
    'verbose': (5, 'LOG_VERBOSE'),
    'log_verbose': (5, 'LOG_VERBOSE'),
}


def read_text_with_fallback(
    path: str | Path,
    encodings: list[str] | tuple[str, ...] | None = None,
) -> str:
    """
    Read a text file using multiple attempted encodings in order.

    Handles BOM automatically via utf-8-sig and utf-16 encodings.
    Raises a clean, descriptive error if all encodings fail.
    """

    if encodings is None:
        # You can reorder these depending on what you expect most commonly.
        encodings = [
            'utf-8-sig',  # handles UTF-8 BOM automatically
            'utf-16',  # auto-detects LE/BE with BOM
            'utf-16-le',
            'utf-16-be',
            'latin-1',  # fallback that never fails (for decoding)
        ]

    path = Path(path)
    raw = path.read_bytes()

    last_error: UnicodeError | None = None

    for encoding in encodings:
        try:
            text = raw.decode(encoding)
            return text
        except UnicodeError as e:
            last_error = e
            continue

    # If we reach here, all decoding attempts failed (only possible if latin-1 is not in encodings).
    raise UnicodeDecodeError(
        'multi-encoding-reader',
        raw,
        0,
        len(raw),
        f"failed to decode file '{path}' using encodings: {', '.join(encodings)}",
    ) from last_error


class DotEnv:
    def __read_dotenv(self, path: str | Path):
        text_data = read_text_with_fallback(path)

        for line in text_data.splitlines():
            line = line.strip()

            # Skip empty lines and comments
            if not line or line.startswith('#'):
                continue

            # Ignore lines that don't contain '=' instead of raising
            if '=' not in line:
                continue

            key, value = line.split('=', 1)
            key = key.strip()
            value = value.strip()

            # Skip lines with empty keys
            if not key:
                continue
            # Strip optional surrounding quotes (must match)
            if len(value) >= 2:
                if (value[0] == '"' and value[-1] == '"') or (value[0] == "'" and value[-1] == "'"):
                    value = value[1:-1]

            self.dotenv_vars[key] = value

    def __init__(self, path: str | Path, environment: str):
        self.dotenv_vars: dict[str, str] = {}

        if isinstance(path, str):
            path = Path(path)

        # Read .env file(s) into environment variables, start from the root of the drive and work our way down.
        paths = list(path.parents[::-1]) + [path]

        # Get the environment specific name. (e.g. .env.develop / .env.production / .env.release)
        env_specific_name = '.env.' + environment

        # Read the .env files.
        for base in paths:
            env_file = base / '.env'
            if env_file.exists():
                self.__read_dotenv(env_file)

            env_file = base / env_specific_name
            if env_file.exists():
                self.__read_dotenv(env_file)

            env_file = base / '.env.local'
            if env_file.exists():
                self.__read_dotenv(env_file)

    def get_string(self, key: str) -> str | None:
        return self.dotenv_vars.get(key)

    def get_all_prefixed(self, prefix: str) -> Mapping[str, str]:
        return {k: v for k, v in self.dotenv_vars.items() if k.startswith(prefix)}

    def get_loglevel(self, key: str) -> int | None:
        value = self.get_string(key)
        if value is None:
            return None

        normalized = value.strip().lower()
        tup = LOGLEVEL_MAP.get(normalized)
        if tup is None:
            raise ValueError(f'Environment variable {key} ({value}) is not a valid log level.')

        return tup[0]


def read(workdir: str | Path, environment_name: str) -> DotEnv:
    return DotEnv(workdir, environment=environment_name)
