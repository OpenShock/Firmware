#include "serial/command_handlers/common.h"

#include "serial/SerialInputHandler.h"

#include <vector>

void _handleVersionCommand(std::string_view arg, bool isAutomated) {
  (void)arg;

  ::Serial.println();
  OpenShock::SerialInputHandler::PrintVersionInfo();
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::VersionHandler() {
  auto group = OpenShock::SerialCmds::CommandGroup("version"sv);

  auto cmd = group.addCommand("Print version information"sv, _handleVersionCommand);

  return group;
}
