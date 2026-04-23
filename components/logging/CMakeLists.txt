# OpenShock logging macros — OS_LOG*, OS_PANIC*.
#
# Split from `common` because it pulls Arduino's esp32-hal-uart.h and
# transitively requires the framework-arduinoespressif32 component. Keeping
# `common` Arduino-clean lets us reach pure IDF faster; merge back once Arduino
# is no longer in the link. log_printf symbol is resolved at final link from
# framework-arduinoespressif32/cores/esp32/esp32-hal-log-wrapper.c.

idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES     time                         # Core.h -> OpenShock::millis()
               log                          # esp_log.h (IDF's own logging)
               app_update                   # esp_ota_ops.h (panic-ota macro)
               esp_system                   # esp_restart
               freertos                     # OS_PANIC -> vTaskDelay
               framework-arduinoespressif32 # esp32-hal-uart.h (log_printf)
)
