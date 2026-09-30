# OpenShock logging + panic macros — OS_LOG*, OS_PANIC*.
#
# openshock_log_printf (Logging.cpp) writes OS_LOG* output to the low-level `serial`
# transport (raw bytes to the console port), not C stdio. The OS_PANIC* macros
# (folded in from the former `panic` component) restart the device, so Logging.h
# pulls in esp_ota_ops / esp_system / freertos and is firmware-only.

idf_component_register(
  SRCS          "src/Logging.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      temporal                        # Temporal.h -> OpenShock::millis() (public header)
                esp_system                      # esp_restart (OS_PANIC* in public header)
                app_update                      # esp_ota_mark_app_invalid_rollback_and_reboot (OS_PANIC_OTA)
                freertos                        # vTaskDelay (OS_PANIC / OS_PANIC_OTA)
  PRIV_REQUIRES serial                          # Serial::Write (Logging.cpp only)
)
