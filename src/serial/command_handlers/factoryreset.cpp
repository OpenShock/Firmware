#include "serial/command_handlers/common.h"

#include "config/Config.h"

static void handleReset(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Command does not support parameters");
    return;
  }

  ::Serial.println("Resetting to factory defaults...");
  OpenShock::Config::FactoryReset();
  ::Serial.println("Restarting...");
  ESP.restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::FactoryResetHandler()
{
  auto group = OpenShock::Serial::CommandGroup("factoryreset"sv);

  auto& cmd = group.addCommand("Reset the device to factory defaults and restart"sv, handleReset);

  return group;
}
