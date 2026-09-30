# OpenShock low-level serial transport — raw byte I/O to the device console
# (UART0 / USB-Serial-JTAG, selected by the ESP-IDF console sdkconfig).
#
# Foundation layer: depends only on the IDF console drivers so `logging` can write
# through it (Serial::Write) without a dependency cycle. It must NOT depend on
# `logging` — hence no OS_LOG here; Init() reports failure via its return value.
idf_component_register(
  SRCS         "src/Serial.cpp"
  INCLUDE_DIRS "include"
  REQUIRES     esp_driver_uart esp_driver_usb_serial_jtag
)
