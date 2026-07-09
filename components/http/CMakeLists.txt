# OpenShock HTTP client manager + content type helpers.
#
# Public surface:
#   include/http/HTTPRequestManager.h — rate-limited HTTP/HTTPS client
#   include/http/ContentTypes.h        — MIME type constants
#
# JsonAPI.{h,cpp} intentionally stays in main because it depends on
# serialization/ + config/ (flatbuffers-heavy) which haven't been extracted.

idf_component_register(
  SRCS          "src/HTTPRequestManager.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      osjson                       # json/Json.h in public header
  PRIV_REQUIRES common                       # Common.h (internal)
                temporal                         # Temporal.h (internal)
                logging                      # Logging.h (internal)
                concurrency                  # SimpleMutex (internal)
                util                         # RateLimiter, StringUtils
                esp_http_client              # esp_http_client.h (internal)
)
