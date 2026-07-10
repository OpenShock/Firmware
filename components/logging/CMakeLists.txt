# OpenShock logging macros — OS_LOG*, OS_PANIC*.
#
# openshock_log_printf (Logging.cpp) writes OS_LOG* output to the low-level `serial`
# transport (raw bytes to the console port), not C stdio. The public header pulls
# in no hardware/IDF include, so it stays host-portable; `serial` is private.

idf_component_register(
  SRCS          "src/Logging.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      temporal                        # Temporal.h -> OpenShock::millis() (public header)
  PRIV_REQUIRES serial                          # Serial::Write (Logging.cpp only)
)
