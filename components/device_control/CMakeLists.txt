# OpenShock device control — the hardware-facing actuators and inputs: estop
# (EStopManager), radio (RFTransmitter), visual status LEDs (VisualStateManager)
# and the shocker CommandHandler that drives the radio.
#
# Extracted out of `core`: its only upstream-core dependency was config, which is
# now its own low-layer component, so device_control depends only on config + the
# foundation/driver layer and nothing else in core. core PRIV_REQUIRES it one-way.
FILE(GLOB_RECURSE device_control_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

idf_component_register(
  SRCS          ${device_control_sources}
  INCLUDE_DIRS  "include"
  REQUIRES      common esp_hal_gpio esp_driver_rmt freertos   # public headers (enums, gpio_types, rmt_tx/encoder, TaskHandle/QueueHandle)
  PRIV_REQUIRES config chipset events led_drivers logging temporal protocols   # concurrency/util folded into common (public REQUIRE)
                esp_driver_gpio esp_wifi esp_event esp_netif soc
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
