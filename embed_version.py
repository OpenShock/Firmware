import os

Import("env")

# This file is invoked by PlatformIO during build.
# See "extra_scripts" in "platformio.ini".

# Fetch environment variables, with sane defaults if not found.
# These variables should be defined by the CI.
openshockApiDomain = os.getenv('OPENSHOCK_API_DOMAIN') or "api.shocklink.net"
openshockFwVersion = os.getenv('OPENSHOCK_FW_VERSION') or "0.8.1"

# Define these variables as macros for expansion during build time.
env.Append(CPPDEFINES=[
    ("OPENSHOCK_API_DOMAIN", env.StringifyMacro(openshockApiDomain)),
    ("OPENSHOCK_FW_VERSION", env.StringifyMacro(openshockFwVersion))
])
