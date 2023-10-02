from utils import pioenv, sysenv, dotenv

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
# If we are running in CI and either building the master branch,
# or building a PR that will merge into the master branch, THEN
# we will build in RELEASE mode.
is_ci = sysenv.get_bool('CI')
is_branch_master = sysenv.get_string('GITHUB_REF_NAME') == 'master'
is_pr_into_master = (
    sysenv.get_string('GITHUB_BASE_REF') == 'master' and sysenv.get_string('GITHUB_EVENT_NAME', '') == 'pull_request'
)
is_release_build = is_ci and (is_branch_master or is_pr_into_master)

# Get the build type string.
pio_build_type = 'release' if is_release_build else 'debug'
dotenv_type = 'production' if is_release_build else 'development'

# Read the correct .env file.
dot = dotenv.DotEnv(project_dir, dotenv_type)

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
    return (flag_dict, [])


# Serialize CPP Defines.
#   Strings are escaped to be a correct CPP macro.
#   Booleans are turned into integers, True => 1 and False => 0.
#   Integers are kept as-is.
def serialize_cpp_defines(raw_defines: dict[str, str | int | bool]) -> dict[str, str | int]:
    result_defines = {}
    for k, v in raw_defines.items():
        try:
            v = int(v)
        except ValueError:
            pass
        if isinstance(v, str):
            result_defines[k] = env.StringifyMacro(v)
        else:
            result_defines[k] = v
    return result_defines


# Fetch the current build flags and group them into (CPP Defines, Other Flags).
raw_build_flags = pio.get_string_array('BUILD_FLAGS', [])
(cpp_defines, remaining_build_flags) = parse_pio_build_flags(raw_build_flags)

# Gets all the environment variables prefixed with 'OPENSHOCK_' and add them as CPP Defines.
openshock_vars = dot.get_all_prefixed('OPENSHOCK_')
cpp_defines.update(openshock_vars)

# Gets the log level from environment variables.
# TODO: Delete get_loglevel and use... something more generic.
log_level_int = dot.get_loglevel('LOG_LEVEL')
if log_level_int is None:
    raise ValueError('LOG_LEVEL must be set in environment variables.')
cpp_defines['CORE_DEBUG_LEVEL'] = log_level_int
cpp_defines = serialize_cpp_defines(cpp_defines)

print('Build type: ' + pio_build_type)
print('Build defines: ' + str(cpp_defines))

# Set PIO variables.
env['BUILD_TYPE'] = pio_build_type
env['BUILD_FLAGS'] = remaining_build_flags
env.Append(CPPDEFINES=list(cpp_defines.items()))
