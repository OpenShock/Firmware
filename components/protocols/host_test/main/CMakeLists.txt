# Compile protocols' PURE encoder sources directly (not via the protocols
# component) so we avoid its firmware-only transitive deps (esp_driver_rmt / logging
# pull in IDF components unavailable on the host). Stubs (stubs/) supply a minimal
# <driver/rmt_types.h> (just rmt_symbol_word_t) and a no-op Logging.h.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_encodebits.cpp"
               "test_caixianlin.cpp"
               "test_sequence.cpp"
               "../../src/CaiXianlinEncoder.cpp"
               "../../src/D80Encoder.cpp"
               "../../src/Petrainer998DREncoder.cpp"
               "../../src/PetrainerEncoder.cpp"
               "../../src/T330Encoder.cpp"
               "../../src/Sequence.cpp"
  INCLUDE_DIRS "."
               "stubs"                      # stub <driver/rmt_types.h> + Logging.h
               "../../include"              # radio/rmt/*Encoder.h, Sequence.h
               "../../src"                  # radio/rmt/internal/Shared.h (EncodeBits)
               "../../../common/include"    # enums/*, OpenShock.h, StringHelpers.h
               "../../../util/include"      # Checksum.h (header-only)
  REQUIRES     unity
  WHOLE_ARCHIVE
)

# OpenShock.h (pulled in via Sequence.h) uses these firmware build-time defines as
# string literals. Supply dummies so the pure logic compiles on the host.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  "OPENSHOCK_API_DOMAIN=\"test\""
  "OPENSHOCK_FW_CDN_DOMAIN=\"test\""
  "OPENSHOCK_FW_VERSION=\"0.0.0-test\""
  "OPENSHOCK_FW_USERAGENT=\"host-test\""
)
