# The config framework. It owns no settings: every module defines its own section (a value type plus a
# Config::Section<T>, config/Section.h) and its own accessors, and keeps it in NVS through what is here:
#
#   config/Nvs.h      typed access to one NVS namespace, the primitive every section's LoadNvs/SaveNvs uses
#   config/Section.h  the section template: RAM cache, serialized writers, saving only what changed, the registry
#   config/Store.h    picking the store (`cfg`, or the shared `nvs` on the 1.x layout), the move between them,
#                     filling an empty store (the 1.x file, else defaults), factory reset, the whole-config JSON
#   config/Wire.h     the whole config as the FlatBuffers HubConfig (local protocol Ready message, rawconfig), and
#                     HubConfig -> JSON, which is also how the 1.x config file is imported
#   config/Migration.h retyping the 1.x littlefs `config` partition as the `cfg` store (run by components/migration)
#   config/Parse.h    field parsing helpers for sections' FromJSON
#
# The 1.x import (Store.cpp's readLegacyConfig, littlefs through fs) is 1.x compatibility code: it goes away on
# master once the release/1.9 branch is cut.
idf_component_register(
  SRCS          "src/Migration.cpp"
                "src/Nvs.cpp"
                "src/Parse.cpp"
                "src/Section.cpp"
                "src/Store.cpp"
                "src/Wire.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common osjson                      # OpenShock.h, SimpleMutex.h, TinyVec.h, util/FunctionRef.h; json/Json.h and json_generator.h
                nvs_flash                          # nvs.h in Nvs.h
                serialization flatbuffers          # HubConfig in Wire.h
                esp_hal_gpio                       # hal/gpio_types.h in Parse.h
  PRIV_REQUIRES fs logging esp_partition           # LfsPartition (the 1.x import), OS_LOG*/OS_PANIC, the partition lookups
                partition_layout esp_system        # the config partition retype, esp_restart
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
