# OpenShock logging macros — OS_LOG*, OS_PANIC*.
#
# openshock_log_printf (Logging.cpp) forwards to vprintf -> the IDF console. Kept
# as its own component so `common` can stay dependency-light — logging pulls in
# the IDF `log` component, `common` does not.

idf_component_register(
  SRCS         "src/Logging.cpp"
  INCLUDE_DIRS "include"
  REQUIRES     temporal                         # Temporal.h -> OpenShock::millis()
               log                          # esp_log.h (IDF's own logging)
)
