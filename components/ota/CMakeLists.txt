# OpenShock OTA update manager: boot-type tracking and app validation/rollback, plus
# the update task that fetches release metadata from the firmware repository and
# flashes the app and the www asset image.
#
# Extracted out of `core` and layered above it (and above captive_portal, which it
# closes before flashing). core reaches back only through the AppHooks callbacks
# registered in OtaUpdateManager::Init().
idf_component_register(
  SRCS          "src/OtaUpdateManager.cpp"
                "src/PartitionUtils.cpp"
                "src/GzipInflater.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common                                          # enums/FirmwareBootType.h, enums/OtaUpdateChannel.h, SemVer.h in public header
  PRIV_REQUIRES core captive_portal config                      # gateway/wifi/WSGateway/AppHooks; CaptivePortal::ForceClose; Config::
                crypto http logging osjson temporal             # Hashing, HTTPRequestManager, json
                www                                             # the www partition the staticfs artifact goes into
                app_update esp_event esp_netif esp_wifi         # esp_ota_ops.h, WiFi/IP event handlers
                esp_partition                                   # flashing partitions from a URL
                heap                                            # heap_caps (the gzip decoder's memory, free-block check)
                espressif__zlib                                 # gzip app images; always required (requirements can't
                                                                # depend on Kconfig), linked only with OPENSHOCK_OTA_GZIP
)

# Match core: IDF compiles application components with -Werror, the legacy build did not.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
