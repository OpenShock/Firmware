# Diagnostics: error-log capture, coredump handling and the opt-in upload of both to the
# backend, plus SD card storage of logs and coredumps (CONFIG_OPENSHOCK_DIAG_SDCARD, off by
# default).
#
# Sits above logging (installs its log tap), http and config (uploads). Nothing in core
# depends on it: app_main calls Init/Update and passes in whether the backend is reachable.
set(diagnostics_sources
  "src/Diagnostics.cpp"
  "src/Uploader.cpp"
  "src/Coredump.cpp"
  "src/RetainedRing.cpp"
  "src/sdcard/SdCardDisabled.cpp")

if(CONFIG_OPENSHOCK_DIAG_SDCARD)
  list(APPEND diagnostics_sources
    "src/sdcard/SdLayout.cpp"
    "src/sdcard/SdHostCard.cpp"
    "src/sdcard/SdStorage.cpp")
endif()

idf_component_register(
  SRCS              ${diagnostics_sources}
  INCLUDE_DIRS      "include"
  PRIV_INCLUDE_DIRS "src"
  REQUIRES          common                   # util/FunctionRef.h in Diagnostics.h
  PRIV_REQUIRES     logging                  # LogTap.h, OS_LOG*
                    temporal                 # OpenShock::millis()
                    http                     # HTTP::Client::Upload
                    config                   # backend domain and token
                    espcoredump              # esp_core_dump_*
                    spi_flash                # esp_flash_read (coredump partition)
                    nvs_flash                # opt-in settings
                    log                      # esp_log_set_vprintf (ESP-IDF's own errors)
                    esp_system
                    app_update               # esp_ota_get_running_partition (BOOT record)
                    esp_app_format
                    sdmmc                    # SD card protocol (sdmmc_card_init, sector read/write)
                    esp_driver_sdmmc         # SD card on the SDMMC host
                    esp_driver_sdspi         # SD card in SPI mode
                    esp_driver_spi           # SPI bus for SPI mode
                    esp_driver_gpio          # SD card detect
                    esp_ringbuf              # SD card record queue
)
