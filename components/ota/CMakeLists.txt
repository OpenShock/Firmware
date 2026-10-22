# OpenShock OTA update manager: boot-type tracking and app validation/rollback, plus
# the update task that fetches release metadata from the firmware repository and
# flashes the app and static filesystem partitions.
#
# Extracted out of `core` and layered above it (and above captive_portal, which it
# closes before flashing). core reaches back only through the AppHooks callbacks
# registered in OtaUpdateManager::Init().
idf_component_register(
  SRCS          "src/OtaUpdateManager.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common                                          # enums/FirmwareBootType.h, enums/OtaUpdateChannel.h, SemVer.h in public header
  PRIV_REQUIRES core captive_portal config                      # gateway/wifi/WSGateway/AppHooks; CaptivePortal::ForceClose; Config::
                crypto fs http hwutil logging osjson temporal   # Hashing, FsCheck, HTTPRequestManager, PartitionUtils, json
                app_update esp_event esp_netif esp_wifi         # esp_ota_ops.h, WiFi/IP event handlers
)

# Match core: IDF compiles application components with -Werror, the legacy build did not.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
