# OpenShock application core.
#
# The tightly-interconnected heart of the firmware: wifi, the gateway link and the
# websocket message handlers, plus the JSON/flatbuffer codecs they share. These form a
# single dependency cycle (wifi <-> gateway <-> message handlers) and therefore live
# here together.
#
# The captive portal and the OTA update manager were split out into captive_portal and
# ota, which sit above core. core calls back into them only through AppHooks.h.
FILE(GLOB_RECURSE core_sources CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.S)

idf_component_register(
  SRCS ${core_sources}
  INCLUDE_DIRS      "include"
  PRIV_INCLUDE_DIRS "src"          # internal headers (GatewayClient.h, message_handlers/impl/*, ...)
  REQUIRES common temporal logging http chipset led_drivers protocols osjson events esp_driver_rmt esp_driver_gpio esp_netif esp_event esp_wifi esp_timer nvs_flash mbedtls flatbuffers serialization config
  # Internal-only now that GatewayClient.h is private:
  PRIV_REQUIRES espressif__esp_websocket_client
                device_control   # estop/CommandHandler/visual used only in core .cpp
)

# IDF compiles application components with -Werror. The legacy PlatformIO build
# applied -Wno-error to our own sources (build_src_flags), so partial designated
# initializers and similar intentional patterns don't fail the build. Match that
# here; can be tightened later by fixing the individual warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
