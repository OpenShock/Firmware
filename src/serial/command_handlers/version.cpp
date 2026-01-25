#include "serial/command_handlers/common.h"

#include "serial/SerialInputHandler.h"

#include <vector>

void _handleVersionCommand(std::string_view arg, bool isAutomated) {
  (void)arg;

  OS_SERIAL.println();
#if ARDUINO_USB_MODE 
  OS_SERIAL_USB.println();
#endif
  OpenShock::SerialInputHandler::PrintVersionInfo();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::VersionHandler() {
  auto group = OpenShock::Serial::CommandGroup("version"sv);

  auto cmd = group.addCommand("Print version information"sv, _handleVersionCommand);

  return group;
}
