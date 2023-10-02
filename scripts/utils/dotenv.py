import os
from pathlib import Path

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
        with open(path, 'r') as f:
            for line in f:
                line = line.strip()
                if line == '' or line.startswith('#'):
                    continue

                key, value = line.strip().split('=', 1)

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

    def get_str(self, key: str, dotenv: bool = True):
        return self.dotenv_vars.get(key, os.environ.get(key))

    def get_all_prefixed(self, prefix: str, dotenv: bool = True):
        result: dict[str, str] = {}
        for key, value in os.environ.items():
            if key.startswith(prefix):
                result[key] = value

        if dotenv:
            for key, value in self.dotenv_vars.items():
                if key.startswith(prefix):
                    result[key] = value

        return result

    def get_loglevel(self, key: str, dotenv: bool = True) -> int | None:
        value = self.get_str(key, dotenv)
        if value == None:
            return None

        value = value.lower()

        tup = LOGLEVEL_MAP.get(value)
        if tup == None:
            raise ValueError('Environment variable ' + key + ' (' + value + ') is not a valid log level.')

        return tup[0]


def read(workdir: str, environment_name: str) -> DotEnv:
    return DotEnv(workdir, environment=environment_name)
