# Compile util's PURE sources directly (not via the util component) so we avoid
# util's firmware-only transitive deps (logging pulls in IDF components that are
# unavailable on the host). A stub Logging.h (stubs/) satisfies the OS_LOG*
# macros the sources use.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_convert.cpp"
               "test_stringutils.cpp"
               "test_hexutils.cpp"
               "test_digitcounter.cpp"
               "test_semver.cpp"
               "test_base64.cpp"
               "test_checksum.cpp"
               "test_tinyvec.cpp"
               "test_fnproxy.cpp"
               "../../src/StringUtils.cpp"
               "../../src/Convert.cpp"
               "../../src/DigitCounter.cpp"
               "../../src/SemVer.cpp"
               "../../src/Base64Utils.cpp"
  INCLUDE_DIRS "."
               "stubs"            # stub Logging.h (shadows the firmware one)
               "../../include"    # util headers (Convert.h, SemVer.h, util/*.h)
               "../../../common/include"  # StringHelpers.h, Common.h
  REQUIRES     unity
               mbedtls            # Base64Utils.cpp -> mbedtls/base64.h
               esp_hal_gpio       # Convert.h -> hal/gpio_types.h (moved out of `hal` in IDF 6.0)
  WHOLE_ARCHIVE
)

# TinyVec.h -> OpenShock.h requires these firmware build-time defines (used as
# string literals). Supply dummies so the pure logic compiles on the host.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  "OPENSHOCK_API_DOMAIN=\"test\""
  "OPENSHOCK_FW_CDN_DOMAIN=\"test\""
  "OPENSHOCK_FW_VERSION=\"0.0.0-test\""
  "OPENSHOCK_FW_USERAGENT=\"host-test\""
)
