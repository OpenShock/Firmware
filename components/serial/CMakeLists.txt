# OpenShock serial console + command handlers.
#
# Separable from `core`: it consumes core's config/wifi/etc. surface, but nothing
# in core depends on it (only the top-level app entry does), so it stands alone as
# a component that REQUIRES core.
FILE(GLOB_RECURSE serial_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

idf_component_register(
  SRCS ${serial_sources}
  INCLUDE_DIRS "include"
  REQUIRES core esp_driver_uart esp_driver_usb_serial_jtag
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
