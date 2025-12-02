#include "serial/command_handlers/CommandGroup.h"
#include "serial/command_handlers/common.h"

#include <esp_system.h>

static void handleRestart(std::string_view arg, bool isAutomated)
{
  (void)arg;

  ::Serial.println("Restarting ESP...");
  esp_restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::RestartHandler()
{
  auto group = OpenShock::Serial::CommandGroup("restart"sv);

  auto& cmd = group.addCommand("Restart the board"sv, handleRestart);

  return group;
}
