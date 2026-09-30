# Compile crypto's PURE sources directly with a stub Logging.h (stubs/) so we
# avoid the firmware-only transitive deps the real Logging.h pulls in. Base64
# pulls in common headers (TinyVec.h -> OpenShock.h), included by path below.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_base64.cpp"
               "../../src/Base64.cpp"
  INCLUDE_DIRS "."
               "stubs"                     # stub Logging.h (shadows the firmware one)
               "../../include"             # crypto headers (Hashing.h, Base64.h)
               "../../../common/include"   # TinyVec.h, OpenShock.h
  REQUIRES     unity
               mbedtls                     # Base64.cpp -> mbedtls/base64.h
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)

# TinyVec.h -> OpenShock.h requires these firmware build-time defines (used as
# string literals). Supply dummies so the pure logic compiles on the host.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  "OPENSHOCK_API_DOMAIN=\"test\""
  "OPENSHOCK_FW_CDN_DOMAIN=\"test\""
  "OPENSHOCK_FW_VERSION=\"0.0.0-test\""
  "OPENSHOCK_FW_USERAGENT=\"host-test\""
)
