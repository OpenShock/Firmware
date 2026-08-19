# OpenShock application core.
#
# The tightly-interconnected heart of the firmware: config, serialization, wifi,
# captive portal, gateway/OTA, radio, visual, estop, command handling and the
# websocket message handlers. These form a single dependency cycle
# (config <-> serialization, wifi <-> captive portal, gateway <-> ota, ...) and
# therefore cannot be split into separate components — they live here together.
FILE(GLOB_RECURSE core_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.S)

idf_component_register(
  SRCS ${core_sources}
  INCLUDE_DIRS      "include"
  PRIV_INCLUDE_DIRS "src"          # internal headers (CaptivePortalInstance.h, GatewayClient.h, message_handlers/impl/*, ...)
  REQUIRES common crypto temporal logging hwutil http chipset led_drivers protocols osjson events esp_driver_rmt esp_driver_gpio esp_netif esp_event esp_wifi esp_timer app_update nvs_flash mbedtls flatbuffers serialization config
  # Internal-only now that CaptivePortalInstance.h + GatewayClient.h are private:
  PRIV_REQUIRES esp_http_server fs dns_server rfc8908 espressif__esp_websocket_client
                device_control   # estop/CommandHandler/visual used only in core .cpp
)

# IDF compiles application components with -Werror. The legacy PlatformIO build
# applied -Wno-error to our own sources (build_src_flags), so partial designated
# initializers and similar intentional patterns don't fail the build. Match that
# here; can be tightened later by fixing the individual warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
