# OpenShock interactive console: SerialInputHandler + command handlers.
#
# The app-level command console: reads lines from the low-level `serial` transport
# and dispatches them against `core` (config, wifi, RF, OTA, ...). Entry-layer;
# nothing in core depends on it (only the top-level app entry does). Its public
# surface is just SerialInputHandler.h, so all deps are private.
FILE(GLOB_RECURSE console_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

idf_component_register(
  SRCS          ${console_sources}
  INCLUDE_DIRS  "include"
  REQUIRES      config                        # its config section (serial_console/SerialInputConfig.h)
  PRIV_REQUIRES core serial device_control   # core: wifi/gateway/http/serialization; serial: Serial::Init/Read; device_control: estop/CommandHandler/visual/radio
                common logging temporal osjson chipset   # used directly; previously only reachable through core's public REQUIRES
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Werror=switch)  # -Wswitch stays fatal: exhaustive enum switches rely on it
