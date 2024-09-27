import os
import random
import subprocess


def __gen_random_uppercase(length=10):
    return ''.join([chr(random.randint(65, 90)) for _ in range(length)])


def __run_get_output(command: list[str], check=True):
    return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=check, text=True).stdout


def set_failed(message: str):
    print('::error::' + message)
    exit(1)


def get_hash() -> str:
    var = os.environ.get('GITHUB_SHA')
    if var is None:
        set_failed('Environment variable "GITHUB_SHA" not found')
    return var  # type: ignore


def get_hash_short():
    return __run_get_output(['git', 'rev-parse', '--short', get_hash()]).strip()


def get_ref():
    var = os.environ.get('GITHUB_REF')
    if var is None:
        set_failed('Environment variable "GITHUB_REF" not found')
    return var


def get_event_name():
    var = os.environ.get('GITHUB_EVENT_NAME')
    if var is None:
        set_failed('Environment variable "GITHUB_EVENT_NAME" not found')
    return var


def set_output(name: str, value: str):
    value = os.environ.get('GITHUB_OUTPUT', '')

    if not value.endswith('\n'):
        value += '\n'

    # Check if value contains newlines
    if '\n' in value:
        # Generate a delimiter that is not in the value
        delimiter = ''
        while delimiter in value:
            delimiter += __gen_random_uppercase()

        value += '{}<<{}\n{}\n{}'.format(name, delimiter, value, delimiter)
    else:
        value += '{}={}'.format(name, value)

    os.environ['GITHUB_OUTPUT'] = value


def ensure_ci(message: str | None = None):
    if os.environ.get('GITHUB_ACTIONS') is not None:
        return

    if message is not None:
        print(message)

    exit(1)
