from utils import envutils

Import('env')

# This file is invoked by PlatformIO during build.
# See 'extra_scripts' in 'platformio.ini'.

# Get PIO variables.
build_flags = env['BUILD_FLAGS'] or []
project_dir = env['PROJECT_DIR']

# By default, the build is run in DEVELOP mode.
# If we are running in CI and either building the master branch,
# or building a PR that will merge into the master branch, THEN
# we will build in RELEASE mode.
is_ci = envutils.get_bool('CI')
is_branch_master = envutils.get_github_ref_name() == 'master'
is_pr_into_master = envutils.get_github_base_ref() == 'master'
is_release = is_ci and (is_branch_master or is_pr_into_master)

# Get the build type string.
builtype = 'release' if is_release else 'develop'

dotenv = envutils.dotenv(project_dir, builtype)

# Defines that will be passed to the compiler.
cpp_defines: dict[str, str | int | bool] = {}

# Gets the build flags from the PIO ini file, removes the -D prefix, splits on the first '=', and then converts to a dictionary.
leftover_flags = []
for flag in build_flags:
    if flag.startswith('-D'):
        flag = flag[2::]
        if '=' in flag:
            (k, v) = flag.split('=', 1)

            cpp_defines[k] = v
        else:
            cpp_defines[flag] = True
    else:
        leftover_flags.append(flag)

# Gets all the environment variables prefixed with 'OPENSHOCK_'.
openshock_vars = dotenv.get_all_prefixed('OPENSHOCK_')
cpp_defines.update(openshock_vars)

# Gets the log level from environment variables.
(loglevel_int, _) = dotenv.get_loglevel('DEBUG_LEVEL')
cpp_defines['CORE_DEBUG_LEVEL'] = loglevel_int

# Convert all the string values to macros.
for k, v in cpp_defines.items():
    try:
        v = int(v)
    except ValueError:
        pass

    if isinstance(v, str):
        cpp_defines[k] = env.StringifyMacro(v)
    else:
        cpp_defines[k] = v

print('Build type: ' + builtype)
print('Build defines: ' + str(cpp_defines))

# Set PIO variables.
env['BUILD_TYPE'] = builtype
env['BUILD_FLAGS'] = leftover_flags
env.Append(CPPDEFINES=[(k, v) for k, v in cpp_defines.items()])
