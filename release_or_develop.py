import os

Import("env")

# This file is invoked by PlatformIO during build.
# See "extra_scripts" in "platformio.ini".

# By default, the build is run in DEVELOP mode.
# If we are running in CI and either building the master branch,
# or building a PR that will merge into the master branch, THEN
# we will build in RELEASE mode.
is_ci = "CI" in os.environ
is_branch_master = "GITHUB_REF_NAME" in os.environ and os.environ["GITHUB_REF_NAME"] == "master"
is_pr_into_master = "GITHUB_BASE_REF" in os.environ and os.environ["GITHUB_BASE_REF"] == "master"

is_release = is_ci and (is_branch_master or is_pr_into_master)

# Log level defines from esp_log.h
LOG_NONE=0
LOG_ERROR=1
LOG_WARN=2
LOG_INFO=3
LOG_DEBUG=4
LOG_VERBOSE=5

# RELEASE build defines.
RELEASE_DEFINES=[
    ("CORE_DEBUG_LEVEL", LOG_INFO)
]

# DEVELOP build defines.
DEVELOP_DEFINES=[
   ("CORE_DEBUG_LEVEL", LOG_VERBOSE)
]

# Set build type and load our defines.
env['BUILD_TYPE'] = is_release and 'release' or 'develop'
env.Append(CPPDEFINES = is_release and RELEASE_DEFINES or DEVELOP_DEFINES)
