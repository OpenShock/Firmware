# The 1.x -> 2.x storage migration, pipelined (docs/STORAGE.md): on a hub still on the 1.x partition table, retype
# `config` as the `cfg` NVS store (config/Migration.h), then write the www image over `static0` and retype it as
# `www` (www/Migration.h). It sits at the top of the component graph: it holds the OTA flash-write lock, closes the
# captive portal and asks the OTA manager for the running release's www image. On a hub already on the 2.x table it
# does nothing and starts no task.
idf_component_register(
  SRCS          "src/StorageMigration.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common                                     # SemVer.h, SimpleMutex.h
  PRIV_REQUIRES config www partition_layout               # the two halves, the live table checks
                ota captive_portal core                   # the flash-write lock and release lookup, ForceClose, WiFiManager
                http logging                              # HTTP::Client, OS_LOG*
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
