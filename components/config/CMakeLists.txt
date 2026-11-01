# The config framework. It owns no settings: every module defines its own section (a value type plus a
# Config::Section<T>, config/Section.h) and its own accessors, and keeps it in NVS through what is here:
#
#   config/Nvs.h      typed access to one NVS namespace, the primitive every section's LoadNvs/SaveNvs uses
#   config/Section.h  the section template: RAM cache, serialized writers, saving only what changed, the registry
#   config/Store.h    opening the `cfg` store, filling an empty one with the defaults, factory reset, the
#                     whole-config JSON
#   config/Parse.h    field parsing helpers for sections' FromJSON
idf_component_register(
  SRCS          "src/Nvs.cpp"
                "src/Parse.cpp"
                "src/Section.cpp"
                "src/Store.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common osjson                      # OpenShock.h, SimpleMutex.h, TinyVec.h, util/FunctionRef.h; json/Json.h and json_generator.h
                nvs_flash                          # nvs.h in Nvs.h
                esp_hal_gpio                       # hal/gpio_types.h in Parse.h
  PRIV_REQUIRES logging                            # OS_LOG*/OS_PANIC
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
