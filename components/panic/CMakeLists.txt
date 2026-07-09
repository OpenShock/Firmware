# Firmware panic/abort macros (OS_PANIC / OS_PANIC_OTA / OS_PANIC_INSTANT),
# split out of `logging` so logging stays portable and host-testable. Header-only.
idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES     logging       # OS_LOGE via OS_PANIC_PRINT
               esp_system    # esp_restart
               app_update    # esp_ota_mark_app_invalid_rollback_and_reboot
               freertos      # vTaskDelay
)
