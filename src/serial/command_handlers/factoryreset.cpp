#include "serial/command_handlers/CommandGroup.h"
#include "serial/command_handlers/common.h"

#include "config/Config.h"

void handleReset(std::string_view arg, bool isAutomated)
{
  (void)arg;

  ::Serial.println("Resetting to factory defaults...");
  OpenShock::Config::FactoryReset();
  ::Serial.println("Restarting...");
  ESP.restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::FactoryResetHandler()
{
  auto group = OpenShock::Serial::CommandGroup("factoryreset"sv);

  auto& cmd = group.addCommand("Reset the hub to factory defaults and restart"sv, handleReset);

  return group;
}
