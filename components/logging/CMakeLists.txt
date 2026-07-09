# OpenShock logging macros — OS_LOG*, OS_PANIC*.
#
# Split from `common` so `common` can stay Arduino-clean. openshock_log_printf
# (Logging.cpp) forwards to vprintf -> the IDF console, so logging no longer
# depends on Arduino's esp32-hal-log wrapper. Merge back into `common` once
# Arduino is out of the link entirely.

idf_component_register(
  SRCS         "src/Logging.cpp"
  INCLUDE_DIRS "include"
  REQUIRES     temporal                         # Temporal.h -> OpenShock::millis()
               log                          # esp_log.h (IDF's own logging)
)
