FILE(GLOB_RECURSE app_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.S)

# The application's headers live in the project-level include/ tree (config/,
# serialization/, radio/, wifi/, ...). PlatformIO adds this to the include path
# globally; native ESP-IDF needs it declared on the component.
idf_component_register(
  SRCS ${app_sources}
  PRIV_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include
  REQUIRES common temporal logging panic concurrency util hwutil http chipset led_drivers protocols osjson events esp_driver_uart esp_driver_usb_serial_jtag esp_driver_rmt esp_driver_gpio esp_http_server esp_netif esp_event esp_wifi esp_timer app_update nvs_flash fs dns_server rfc8908 mbedtls espressif__esp_websocket_client flatbuffers
)

# IDF compiles application components with -Werror. The legacy PlatformIO build
# applied -Wno-error to our own sources (build_src_flags), so partial designated
# initializers and similar intentional patterns don't fail the build. Match that
# here; can be tightened later by fixing the individual warnings.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
