#include "serial/command_handlers/common.h"
#include "serial/SerialInputHandler.h"

#include <esp_system.h>

void _handleRestartCommand(std::string_view arg, bool isAutomated) {
  (void)arg;

  OS_SERIAL.println("Restarting ESP...");
#if ARDUINO_USB_MODE 
  OS_SERIAL_USB.println("Restarting ESP...");
#endif
  esp_restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::RestartHandler() {
  auto group = OpenShock::Serial::CommandGroup("restart"sv);

  auto& cmd = group.addCommand("Restart the board"sv, _handleRestartCommand);

  return group;
}
