#include "serial/command_handlers/CommandGroup.h"
#include "serial/command_handlers/common.h"

#include "serial/SerialInputHandler.h"

#include <vector>

static void handleVersion(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Command does not support parameters");
    return;
  }

  ::Serial.println();
  OpenShock::SerialInputHandler::PrintVersionInfo();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::VersionHandler()
{
  auto group = OpenShock::Serial::CommandGroup("version"sv);

  auto cmd = group.addCommand("Print version information"sv, handleVersion);

  return group;
}
