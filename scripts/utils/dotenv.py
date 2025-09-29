import os
from pathlib import Path
from typing import Mapping

LOGLEVEL_MAP = {
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


class DotEnv:
    def __read_dotenv(self, path: str | Path):
        text_data = ''

        with open(path, 'rb') as f:  # Open the file in binary mode first to detect BOM
            raw_data = f.read()

            # Check for BOM and strip it if present
            if raw_data.startswith(b'\xef\xbb\xbf'):  # UTF-8 BOM
                text_data = raw_data[3:].decode('utf-8')
            elif raw_data.startswith(b'\xff\xfe'):  # UTF-16 LE BOM
                text_data = raw_data[2:].decode('utf-16le')
            elif raw_data.startswith(b'\xfe\xff'):  # UTF-16 BE BOM
                text_data = raw_data[2:].decode('utf-16be')

        # Now process the text data
        for line in text_data.splitlines():
            line = line.strip()
            if line == '' or line.startswith('#'):
                continue

            split = line.strip().split('=', 1)
            if len(split) != 2:
                print('Failed to parse: ' + line)
                continue

            self.dotenv_vars[line[0]] = line[1]

    def __init__(self, path: str | Path, environment: str):
        self.dotenv_vars: dict[str, str] = {}

        if isinstance(path, str):
            path = Path(path)

        # Read .env file(s) into environment variables, start from the root of the drive and work our way down.
        paths = list(path.parents[::-1]) + [path]

        # Get the environment specific name. (e.g. .env.develop / .env.production / .env.release)
        env_specific_name = '.env.' + environment

        # Read the .env files.
        for path in paths:
            env_file = path / '.env'
            if env_file.exists():
                self.__read_dotenv(env_file)

            env_file = path / env_specific_name
            if env_file.exists():
                self.__read_dotenv(env_file)

            env_file = path / '.env.local'
            if env_file.exists():
                self.__read_dotenv(env_file)

    def get_string(self, key: str):
        return self.dotenv_vars.get(key)

    def get_all_prefixed(self, prefix: str) -> Mapping[str, str]:
        result: dict[str, str] = {}
        for key, value in self.dotenv_vars.items():
            if key.startswith(prefix):
                result[key] = value
        return result

    def get_loglevel(self, key: str) -> int | None:
        value = self.get_string(key)
        if value == None:
            return None

        value = value.lower()

        tup = LOGLEVEL_MAP.get(value)
        if tup == None:
            raise ValueError('Environment variable ' + key + ' (' + value + ') is not a valid log level.')

        return tup[0]


def read(workdir: str, environment_name: str) -> DotEnv:
    return DotEnv(workdir, environment=environment_name)
