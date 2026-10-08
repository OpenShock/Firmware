# OpenShock crypto helpers — the utilities that pull in mbedTLS, split out of
# `common` so common stays free of the mbedTLS dependency.
#
# Public surface:
#   Hashing.h            — MD5 / SHA1 / SHA256 hashers (mbedtls/md.h)
#   Base64.h             — base64 encode/decode (mbedtls/base64.h in the .cpp)

idf_component_register(
  SRCS          "src/Base64.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common       # OpenShock.h (Hashing.h), TinyVec.h (Base64.h) in public headers
                mbedtls       # mbedtls/md.h in Hashing.h public header
  PRIV_REQUIRES logging       # Logging.h (Base64.cpp)
)
