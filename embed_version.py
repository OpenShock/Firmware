import os

Import("env")

shocklinkApiUrl = os.getenv('SHOCKLINK_API_URL') or "api.shocklink.net"
shocklinkFwVersion = os.getenv('SHOCKLINK_FW_VERSION') or "0.8.1"

env.Append(CPPDEFINES=[
    ("SHOCKLINK_API_URL", env.StringifyMacro(shocklinkApiUrl)),
    ("SHOCKLINK_FW_VERSION", env.StringifyMacro(shocklinkFwVersion))
])
