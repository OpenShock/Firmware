#include "serial/command_handlers/common.h"

#include "serial/SerialInputHandler.h"

#include <cstdio>
#include <vector>

void _handleVersionCommand(std::string_view arg, bool isAutomated) {
  (void)arg;

  putchar('\n');

  OpenShock::SerialInputHandler::PrintVersionInfo();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::VersionHandler() {
  auto group = OpenShock::Serial::CommandGroup("version"sv);

  auto cmd = group.addCommand("Print version information"sv, _handleVersionCommand);

  return group;
}
