# The hardware-free parts of diagnostics: the retained error ring and the SD card layout
# (format, classification, ring head recovery, record packing, dump slots), against an
# in-memory block device. test_sdlayout.cpp also writes the golden card image that
# tools/sdlog's tests read (see test_golden_image).
idf_component_register(
  SRCS         "test_main.cpp"
               "test_retained_ring.cpp"
               "test_sdlayout.cpp"
               "../../src/RetainedRing.cpp"
               "../../src/sdcard/SdLayout.cpp"
  INCLUDE_DIRS "."
               "../../src"                  # RetainedRing.h, sdcard/*
               "../../include"              # diagnostics/SdCard.h
               "../../../common/include"    # Checksum.h, util/FunctionRef.h
  REQUIRES     unity
               host_support                 # Logging.h, openshock_board.h (test/host_support)
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)
