FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
  SRCS ${app_sources}
  REQUIRES common temporal logging panic concurrency util hwutil http chipset led_drivers protocols osjson events esp_driver_uart esp_driver_usb_serial_jtag esp_driver_rmt
)
