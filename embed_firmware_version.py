
Import("env")

env.Append(CPPDEFINES=[
    ("SHOCKLINK_API_URL", env.StringifyMacro("api.shocklink.net")),
    ("SHOCKLINK_DEV_API_URL", env.StringifyMacro("dev-api.shocklink.net")),
    ("SHOCKLINK_FW_VERSION", env.StringifyMacro("0.8.1"))
])

print(env.Dump())
