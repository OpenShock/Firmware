import os

Import("env")

# This file is invoked by PlatformIO during build.
# See "extra_scripts" in "platformio.ini".

# Fetch environment variables, with sane defaults if not found.
# These variables should be defined by the CI.
shocklinkApiUrl = os.getenv('SHOCKLINK_API_URL') or "api.shocklink.net"
shocklinkFwVersion = os.getenv('SHOCKLINK_FW_VERSION') or "0.8.1"

# Define these variables as macros for expansion during build time.
env.Append(CPPDEFINES=[
    ("SHOCKLINK_API_URL", env.StringifyMacro(shocklinkApiUrl)),
    ("SHOCKLINK_FW_VERSION", env.StringifyMacro(shocklinkFwVersion))
])
