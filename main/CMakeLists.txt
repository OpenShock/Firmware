# Application entry point only. All firmware logic lives in components/core (the
# interconnected subsystem tangle) and components/serial_console (the interactive
# console); main just wires them together in app_main.
idf_component_register(
  SRCS "main.cpp"
  REQUIRES core serial_console device_control events logging common nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
