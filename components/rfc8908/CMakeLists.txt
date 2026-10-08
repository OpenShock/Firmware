# RFC 8908 + classic captive-detection HTTP handlers for esp_http_server: OS probe
# redirects and the machine-readable /captive-portal/api endpoint. Standalone so it's
# independent of the portal's app logic and the DNS responder.
idf_component_register(
  SRCS "src/RFC8908Handler.cpp"
  INCLUDE_DIRS "include"
  REQUIRES esp_http_server          # esp_http_server.h in public header
  PRIV_REQUIRES logging osjson      # .cpp-only (common was unused)
)
