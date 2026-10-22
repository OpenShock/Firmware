# OpenShock HTTP client manager + content type helpers.
#
# Public surface:
#   include/http/HTTPRequestManager.h: rate-limited HTTP/HTTPS client
#   include/http/ContentTypes.h:        MIME type constants
#
# The backend endpoint wrappers (http/JsonAPI.h) live in core, not here, because they
# depend on config/ and the core serialization codecs.

idf_component_register(
  SRCS          "src/HTTPRequestManager.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      osjson                       # json/Json.h in public header
  PRIV_REQUIRES common                       # Common.h, RateLimiter, StringHelpers, SimpleMutex (internal)
                temporal                         # Temporal.h (internal)
                logging                      # Logging.h (internal)
                esp_http_client              # esp_http_client.h (internal)
                mbedtls                      # esp_crt_bundle.h (internal)
)
