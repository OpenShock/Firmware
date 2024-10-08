from typing import Mapping
from utils import pioenv, sysenv, dotenv, shorthands
import git
import re

# This file is invoked by PlatformIO during build.
# See 'extra_scripts' in 'platformio.ini'.

#######################################################
#           DETERMINING THE ENVIRONMENT               #
#######################################################

# Import the PlatformIO env and initialize our PioEnv wrapper.
Import('env')  # type: ignore
pio = pioenv.PioEnv(env)  # type: ignore

# Get PIO variables.
project_dir = pio.get_string('PROJECT_DIR')

# By default, the build is run in DEVELOP mode.
# If we are running in CI and either building the master branch, beta branch, or a tag, then we are in RELEASE mode.
is_ci = shorthands.is_github_ci()
branch_name = shorthands.get_github_ref_name()
is_release_build = is_ci and (
    branch_name == 'master'
    or shorthands.is_github_pr_into('master')
    or branch_name == 'beta'
    or shorthands.is_github_pr_into('beta')
    or shorthands.is_github_tag()
)

# Get the build type string.
pio_build_type = 'release' if is_release_build else 'debug'
dotenv_type = 'production' if is_release_build else 'development'

# Read the correct .env file.
dot = dotenv.DotEnv(project_dir, dotenv_type)

def get_git_repo():
    try:
        return git.Repo(search_parent_directories=True)
    except git.exc.InvalidGitRepositoryError:
        return None

def sort_semver(versions):
    if not versions or len(versions) == 0:
        return []
    
    def parse_semver(v):
        # Split version into main, prerelease, and build metadata parts
        match = re.match(r'^v?(\d+(?:\.\d+)*)(?:-([0-9A-Za-z-.]+))?(?:\+([0-9A-Za-z-.]+))?$', v)
        if not match:
            raise ValueError(f"Invalid version: {v}")
        main_version, prerelease, build = match.groups()
        
        return (main_version, prerelease, build)

    def version_key(version):
        main_version, _, _ = parse_semver(version)

        # Convert the main version into a tuple of integers for natural sorting
        main_version_tuple = tuple(map(int, main_version.split('.')))

        return main_version_tuple

    # Remove versions with prerelease/build suffixes
    clean_only = [v for v in versions if not ('-' in v or '+' in v)]

    # Sort by version key
    return sorted(clean_only, key=lambda v: version_key(v))

def last_element(arr):
    return arr[-1] if len(arr) > 0 else None

git_repo = get_git_repo()
git_commit = git_repo.head.object.hexsha if git_repo is not None else None
git_tags = [tag.name for tag in git_repo.tags] if git_repo is not None else []
git_latest_clean_tag = last_element(sort_semver(git_tags))

# Find env variables based on only the pioenv and sysenv.
def get_pio_firmware_vars() -> dict[str, str | int | bool]:
    fw_board = pio.get_string('PIOENV')
    fw_chip = pio.get_string('BOARD_MCU')

    def macroify(s: str) -> str:
        return s.upper().replace('-', '').replace('_', '')

    vars = {}
    vars['OPENSHOCK_FW_BOARD'] = fw_board
    vars['OPENSHOCK_FW_BOARD_' + macroify(fw_board)] = True  # Used for conditional compilation.
    vars['OPENSHOCK_FW_CHIP'] = fw_chip.upper()
    vars['OPENSHOCK_FW_CHIP_' + macroify(fw_chip)] = True  # Used for conditional compilation.
    vars['OPENSHOCK_FW_MODE'] = pio_build_type
    if git_commit is not None:
        vars['OPENSHOCK_FW_GIT_COMMIT'] = git_commit
    return vars


#######################################################
#             UPDATING BUILD PARAMETERS               #
#######################################################


# Parse PIO build flags.
#   All CPP Defines, i.e. flags starting with "-D", are parsed for key/value and stored in a dictionary (first value).
#   All other build flags are returned in a list (second value)
def parse_pio_build_flags(raw_flags: list[str]) -> tuple[dict[str, str | int | bool], list[str]]:
    flag_dict = {}
    leftover_flags = []
    for flag in raw_flags:
        if flag.startswith('-D'):
            flag = flag[2::]
            if '=' in flag:
                (k, v) = flag.split('=', 1)
                flag_dict[k] = v
            else:
                flag_dict[flag] = True
        else:
            leftover_flags.append(flag)
    return (flag_dict, leftover_flags)


def transform_cpp_define_string(k: str, v: str) -> str:
    # Remove surrounding quotes if present.
    if v.startswith('"') and v.endswith('"'):
        v = v[1:-1]

    return env.StringifyMacro(v)


def serialize_cpp_define(k: str, v: str | int | bool) -> str | int:
    # Special case for OPENSHOCK_FW_GIT_COMMIT.
    if k == 'OPENSHOCK_FW_GIT_COMMIT':
        return transform_cpp_define_string(k, str(v))

    try:
        return int(v)
    except ValueError:
        pass
    if isinstance(v, str):
        return transform_cpp_define_string(k, v)

    # TODO: Handle booleans.

    return v


# Serialize CPP Defines.
#   Strings are escaped to be a correct CPP macro.
#   Booleans are turned into integers, True => 1 and False => 0.
#   Integers are kept as-is.
def serialize_cpp_defines(raw_defines: dict[str, str | int | bool]) -> dict[str, str | int]:
    result_defines = {}
    for k, v in raw_defines.items():
        result_defines[k] = serialize_cpp_define(k, v)
    return result_defines


# Copy key/value pairs from "src" into "dest" only if those keys don't exist in "dest" yet.
def merge_missing_keys(dest: dict[str, str | int | bool], src: Mapping[str, str | int | bool]):
    for k, v in src.items():
        if k not in dest:
            dest[k] = v


# Print a dictionary for debugging purposes.
def print_dump(name: str, map: Mapping[str, str | int | bool]) -> None:
    print('%s:' % name)
    for k, v in map.items():
        print('  %s = %s' % (k, v))


# Fetch the current build flags and group them into (CPP Defines, Other Flags).
raw_build_flags = pio.get_string_array('BUILD_FLAGS', [])
(cpp_defines, remaining_build_flags) = parse_pio_build_flags(raw_build_flags)

# Gets all the environment variables prefixed with 'OPENSHOCK_' and add them as CPP Defines.
sys_openshock_vars = sysenv.get_all_prefixed('OPENSHOCK_')
pio_openshock_vars = get_pio_firmware_vars()
dot_openshock_vars = dot.get_all_prefixed('OPENSHOCK_')

print_dump('Sys OpenShock vars', sys_openshock_vars)
print_dump('PIO OpenShock vars', pio_openshock_vars)
print_dump('Dotenv OpenShock vars', dot_openshock_vars)

merge_missing_keys(cpp_defines, sys_openshock_vars)
merge_missing_keys(cpp_defines, pio_openshock_vars)
merge_missing_keys(cpp_defines, dot_openshock_vars)

# Check if OPENSHOCK_FW_VERSION is set.
if 'OPENSHOCK_FW_VERSION' not in cpp_defines:
    if is_ci:
        raise ValueError('OPENSHOCK_FW_VERSION must be set in environment variables for CI builds.')
    
    # If latest_git_tag is set, use it, else use 0.0.0, assign to variable.
    version = (git_latest_clean_tag if git_latest_clean_tag is not None else '0.0.0') + '-local'

    # If git_commit is set, append it to the version.
    if git_commit is not None:
        version += '+' + git_commit[0:7]
    
    # If not set, get the latest tag.
    cpp_defines['OPENSHOCK_FW_VERSION'] = version

# Gets the log level from environment variables.
# TODO: Delete get_loglevel and use... something more generic.
log_level_int = dot.get_loglevel('LOG_LEVEL')
if log_level_int is None:
    raise ValueError('LOG_LEVEL must be set in environment variables.')
cpp_defines['OPENSHOCK_LOG_LEVEL'] = log_level_int
cpp_defines['CORE_DEBUG_LEVEL'] = 2 # Warning level. (FUCK Arduino)

# Serialize and inject CPP Defines.
print_dump('CPP Defines', cpp_defines)

cpp_defines = serialize_cpp_defines(cpp_defines)

print('Build type: ' + pio_build_type)
print('Build defines: ' + str(cpp_defines))

# Set PIO variables.
env['BUILD_TYPE'] = pio_build_type
env['BUILD_FLAGS'] = remaining_build_flags
env.Append(CPPDEFINES=list(cpp_defines.items()))

# Rename the firmware.bin to app.bin.
env.Replace(PROGNAME='app')

print(env.Dump())
