
Import("env")

env.Append(CPPDEFINES=[
    ("SHOCKLINK_API_URL", "\"api.shocklink.net\""),
    ("SHOCKLINK_DEV_API_URL", "\"dev-api.shocklink.net\""),
    ("SHOCKLINK_FW_VERSION", "\"0.8.1\"")
])

print(env.Dump())
