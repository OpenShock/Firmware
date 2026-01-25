#include "serial/command_handlers/common.h"
#include "serial/SerialInputHandler.h"

#include "config/Config.h"

#include <esp_system.h>

void _handleFactoryResetCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  OS_SERIAL.println("Resetting to factory defaults...");
#if ARDUINO_USB_MODE 
  OS_SERIAL_USB.println("Resetting to factory defaults...");
#endif
  OpenShock::Config::FactoryReset();
  OS_SERIAL.println("Restarting...");
#if ARDUINO_USB_MODE 
  OS_SERIAL_USB.println("Restarting...");
#endif
  esp_restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::FactoryResetHandler()
{
  auto group = OpenShock::Serial::CommandGroup("factoryreset"sv);

  auto& cmd = group.addCommand("Reset the hub to factory defaults and restart"sv, _handleFactoryResetCommand);

  return group;
}
