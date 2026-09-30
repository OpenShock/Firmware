#include "serial_console/command_handlers/common.h"
#include "serial_console/SerialInputHandler.h"

#include <esp_system.h>

static void handleRestartCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  OS_SERIAL_PRINTLN("Restarting ESP...");
  esp_restart();
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::RestartHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("restart"sv);

  auto& cmd = group.addCommand("Restart the board"sv, handleRestartCommand);

  return group;
}
