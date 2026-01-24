#include "serial/command_handlers/common.h"

#include "config/Config.h"

#include <esp_system.h>

void _handleFactoryResetCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  ::Serial.println("Resetting to factory defaults...");
  OpenShock::Config::FactoryReset();
  ::Serial.println("Restarting...");
  esp_restart();
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::FactoryResetHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("factoryreset"sv);

  auto& cmd = group.addCommand("Reset the hub to factory defaults and restart"sv, _handleFactoryResetCommand);

  return group;
}
