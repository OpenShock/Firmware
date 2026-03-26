#include "serial/command_handlers/common.h"

#include "visual/VisualStateManager.h"

void _handleSerialLedTestCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;
  (void)isAutomated;

  SERPR_RESPONSE("LedTest|Starting LED test (~24s)...");

  OpenShock::VisualStateManager::RunLedTest();

  SERPR_SUCCESS("LedTest|Complete");
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::LedTestHandler()
{
  auto group = OpenShock::Serial::CommandGroup("ledtest"sv);

  group.addCommand("Cycle through all LED patterns for visual verification"sv, _handleSerialLedTestCommand);

  return group;
}
