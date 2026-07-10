# OpenShock configuration model — typed config structs with JSON + flatbuffer
# (de)serialization, persisted to littlefs via the fs component.
#
# Extracted out of `core` into a low-layer component: it depends only on the
# generated wire schema (serialization), JSON, the config filesystem and a few
# foundation utilities — nothing in core. This is what lets device_control (estop/
# radio/visual/CommandHandler, which read Config::) leave core in a later step
# without a config <-> core cycle.
FILE(GLOB_RECURSE config_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

idf_component_register(
  SRCS          ${config_sources}
  INCLUDE_DIRS  "include"
  REQUIRES      common osjson util serialization flatbuffers   # in public headers (enums, json/Json.h, HubConfig fbs, TinyVec, flatbuffers::Offset)
                esp_wifi esp_hal_gpio                           # WiFiCredentials.h -> esp_wifi_types.h (wifi_auth_mode_t); RFConfig.h -> hal/gpio_types.h (gpio_num_t)
  PRIV_REQUIRES chipset fs logging panic concurrency            # .cpp only (GPIO validation, ConfigFs, OS_LOG*, OS_PANIC, ReadWriteMutex)
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
