# Diagnostics: crash dumps and logs, uploaded (CONFIG_OPENSHOCK_DIAG_UPLOAD) and/or stored on an SD card
# (CONFIG_OPENSHOCK_DIAG_SDCARD). With neither, the component is empty and its callers leave it alone
# (CONFIG_OPENSHOCK_DIAG).
#
# Sits above logging (installs its log tap), http and config (uploads). Nothing in core
# depends on it: app_main calls Init/Update and passes in whether the backend is reachable.
set(diagnostics_sources)

if(CONFIG_OPENSHOCK_DIAG)
  list(APPEND diagnostics_sources
    "src/Diagnostics.cpp"
    "src/Coredump.cpp")
endif()

if(CONFIG_OPENSHOCK_DIAG_UPLOAD)
  list(APPEND diagnostics_sources
    "src/Upload.cpp"
    "src/Uploader.cpp"
    "src/RetainedRing.cpp")
endif()

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
                    config                   # upload opt-ins, backend domain and token
                    espcoredump              # esp_core_dump_*
                    spi_flash                # esp_flash_read (coredump partition)
                    nvs_flash                # which coredump the backend already has
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
