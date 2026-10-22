# Compile common's PURE sources directly; Logging.h and the generated
# openshock_board.h that OpenShock.h includes come from test/host_support, which
# also supplies the real CONFIG_OPENSHOCK_* Kconfig values. Absorbed the former
# `util` host test after util was merged into common.
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
               "../../include"  # common's headers (enums/*, Convert.h, SemVer.h, util/*.h)
  REQUIRES     unity
               host_support     # Logging.h, openshock_board.h (test/host_support)
               esp_hal_gpio     # Convert.h -> hal/gpio_types.h (moved out of `hal` in IDF 6.0)
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)
