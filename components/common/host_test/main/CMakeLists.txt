# Compile common's PURE sources directly with a stub Logging.h (stubs/) so we avoid
# the firmware-only transitive deps that the real Logging.h pulls in (esp_system /
# esp_ota_ops / serial). Absorbed the former `util` host test after util was merged
# into common.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_enums.cpp"
               "test_stringhelpers.cpp"
               "test_stringutils.cpp"
               "test_convert.cpp"
               "test_hexutils.cpp"
               "test_digitcounter.cpp"
               "test_semver.cpp"
               "test_checksum.cpp"
               "test_tinyvec.cpp"
               "test_fnproxy.cpp"
               "../../src/StringHelpers.cpp"  # StringSplit/StringIContains/FormatToString (out-of-line)
               "../../src/Convert.cpp"
               "../../src/DigitCounter.cpp"
               "../../src/SemVer.cpp"
  INCLUDE_DIRS "."
               "stubs"          # stub Logging.h (shadows the firmware one)
               "../../include"  # common's headers (enums/*, Convert.h, SemVer.h, util/*.h)
  REQUIRES     unity
               esp_hal_gpio     # Convert.h -> hal/gpio_types.h (moved out of `hal` in IDF 6.0)
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
