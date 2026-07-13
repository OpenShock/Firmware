# OpenShock foundation utilities — language helpers, format conversions,
# containers, string helpers, and the FreeRTOS-backed concurrency primitives.
# Absorbed the former `util` and `concurrency` components. The mbedTLS-dependent
# helpers (Hashing.h, Base64) live in the separate `crypto` component.
#
# Public surface (include/):
#   Common.h / FormatHelpers.h / OpenShock.h — basic defines and macros
#   enums/*.h        — shared enums (consumers include with the enums/ prefix)
#   Convert.h, SemVer.h, Checksum.h, RateLimiter.h, TinyVec.h,
#   StringHelpers.h, SimpleMutex.h, ReadWriteMutex.h, util/*.h

idf_component_register(
  SRCS          "src/Convert.cpp"
                "src/DigitCounter.cpp"
                "src/RateLimiter.cpp"
                "src/ReadWriteMutex.cpp"
                "src/SemVer.cpp"
                "src/SimpleMutex.cpp"
                "src/StringHelpers.cpp"
                "src/TaskUtils.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      logging      # Logging.h / OS_PANIC* (TinyVec.h) in public headers
                temporal     # Temporal.h (RateLimiter.h) in public header
                freertos     # semphr.h in SimpleMutex.h / ReadWriteMutex.h public headers
)
