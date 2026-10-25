# OpenShock foundation utilities: language helpers, format conversions,
# containers, string helpers, and the FreeRTOS-backed concurrency primitives.
# Absorbed the former `util` and `concurrency` components. The mbedTLS-dependent
# helpers (Hashing.h, Base64) live in the separate `crypto` component.
#
# Public surface (include/):
#   Common.h / FormatHelpers.h / OpenShock.h: basic defines and macros
#   BoardDescriptor.h: the board descriptor stamped into the app image, Board::Get()
#   enums/*.h:        shared enums (consumers include with the enums/ prefix)
#   Convert.h, SemVer.h, Checksum.h, RateLimiter.h, TinyVec.h,
#   StringHelpers.h, SimpleMutex.h, ReadWriteMutex.h, util/*.h

idf_component_register(
  SRCS          "src/BoardDescriptor.cpp"       # Board::Get() / Parse()
                "src/BoardDescriptorImage.cpp"  # the placeholder descriptor in .rodata_custom_desc (not in host tests)
                "src/Convert.cpp"
                "src/RateLimiter.cpp"
                "src/ReadWriteMutex.cpp"
                "src/SemVer.cpp"
                "src/SimpleMutex.cpp"
                "src/StringHelpers.cpp"
                "src/TaskUtils.cpp"
                # The only TU that sees openshock_version.h, so a new commit rebuilds
                # this file rather than the whole project.
                "src/Version.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      logging      # Logging.h / OS_PANIC* (TinyVec.h) in public headers
                temporal     # Temporal.h (RateLimiter.h) in public header
                freertos     # semphr.h in SimpleMutex.h / ReadWriteMutex.h public headers
)

# Keep the board descriptor in the link even when nothing references it. The ESP-IDF app
# image format documentation prescribes this for a custom structure in .rodata_custom_desc:
# the component is a static archive, so an unreferenced object would otherwise be dropped
# and the image would carry no descriptor at offset 0x120 for scripts/stamp_board.py.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u openshock_board_desc")
