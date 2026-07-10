#include "serial/command_handlers/common.h"

#include "visual/VisualStateManager.h"

static void handleSerialLedTestCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;
  (void)isAutomated;

  SERPR_RESPONSE("LedTest|Starting LED test (~24s)...");

  OpenShock::VisualStateManager::RunLedTest();

  SERPR_SUCCESS("LedTest|Complete");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::LedTestHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("ledtest"sv);

  group.addCommand("Cycle through all LED patterns for visual verification"sv, handleSerialLedTestCommand);

  return group;
}
