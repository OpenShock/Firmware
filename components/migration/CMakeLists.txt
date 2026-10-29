# The one-time 1.x -> 2.x partition layout migration of the 1.9.x pivot release (docs/STORAGE.md). It sits at the
# top of the component graph: it drives the OTA manager, closes the captive portal and holds the config store while
# it rewrites the partition table. A 2.x build only reports the layout; the 1.x specifics go away with release/1.9,
# the generic pieces (partition_layout's checks, the table write) stay for a later layout change.
idf_component_register(
  SRCS          "src/LayoutMigration.cpp"
  INCLUDE_DIRS  "include"
  PRIV_REQUIRES ota captive_portal core config partition_layout www   # OtaUpdateManager, ForceClose, WiFiManager + OTA config, write lock, the table checks, www format
                device_control                                     # VisualStateManager (the do-not-unplug LED)
                common crypto http logging                         # SemVer/TaskUtils, SHA256, HTTP::Client, OS_LOG*
                app_update bootloader_support esp_partition spi_flash nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
