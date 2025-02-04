#include "serial/command_handlers/common.h"

#include "config/Config.h"

#include <esp_system.h>

#include <cstdio>

void _handleFactoryResetCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  printf("Resetting to factory defaults...\n");
  OpenShock::Config::FactoryReset();
  printf("Restarting...\n");
  esp_restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::FactoryResetHandler()
{
  auto group = OpenShock::Serial::CommandGroup("factoryreset"sv);

  auto& cmd = group.addCommand("Reset the hub to factory defaults and restart"sv, _handleFactoryResetCommand);

  return group;
}
