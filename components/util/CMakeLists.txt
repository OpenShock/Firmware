# OpenShock leaf utilities — format conversions, hashing, containers, etc.
#
# Public surface under include/ (flat top-level files) and include/util/ (nested).

idf_component_register(
  SRCS          "src/Base64Utils.cpp"
                "src/Convert.cpp"
                "src/DigitCounter.cpp"
                "src/IPAddressUtils.cpp"
                "src/ParitionUtils.cpp"
                "src/RateLimiter.cpp"
                "src/SemVer.cpp"
                "src/StringUtils.cpp"
                "src/TaskUtils.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common       # Common.h, TinyVec.h in public headers
                logging      # Logging.h (OS_LOG* used inline in headers)
                time         # Core.h used in header (RateLimiter)
                concurrency  # SimpleMutex used in header (RateLimiter)
                mbedtls      # Hashing (md5/sha1/sha256) uses mbedtls types in headers
  PRIV_REQUIRES http         # ParitionUtils.cpp uses HTTPRequestManager (internal)
                freertos     # TaskUtils implementation
)
