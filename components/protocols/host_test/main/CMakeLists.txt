# Compile protocols' PURE encoder sources directly (not via the protocols component)
# so we avoid its firmware-only transitive deps (common -> logging -> app_update has
# no linux port). rmt_symbol_word_t comes from ESP-IDF's own RMT headers, through the
# driver mock; Logging.h and openshock_board.h from test/host_support.
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
               "../../include"              # radio/rmt/*Encoder.h, Sequence.h
               "../../src"                  # radio/rmt/internal/Shared.h (EncodeBits)
               "../../../common/include"    # enums/*, OpenShock.h, Checksum.h
  REQUIRES     unity
               host_support                 # Logging.h, openshock_board.h (test/host_support)
               driver                       # <driver/rmt_types.h> (tools/mocks/driver on linux)
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)
