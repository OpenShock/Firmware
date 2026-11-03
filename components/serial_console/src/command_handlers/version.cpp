#include "serial_console/command_handlers/common.h"

#include "serial_console/SerialInputHandler.h"

#include <vector>

static void handleVersionCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  OS_SERIAL_PRINTLN();
  OpenShock::SerialInputHandler::PrintVersionInfo();
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::VersionHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("version");

  group.addCommand("Print version information", handleVersionCommand);

  return group;
}
