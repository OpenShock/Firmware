#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "Convert.h"
#include "StringHelpers.h"
#include "visual/VisualStateManager.h"

static void handleStatusLedCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("StatusLed|%s", OpenShock::VisualStateManager::GetStatusLedsEnabled() ? "true" : "false");
    return;
  }

  auto enabled = OpenShock::Convert::ToBool(OpenShock::StringTrim(arg));
  if (!enabled) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  OpenShock::SerialCmds::ReportSaved(OpenShock::VisualStateManager::SetStatusLedsEnabled(*enabled));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::StatusLedHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("statusled");

  group.addCommand("Get whether the status LEDs show the hub's state", handleStatusLedCommand);

  auto& setCommand = group.addCommand("Turn the status LEDs on or off. When off they only light up for an active E-Stop or a critical error", handleStatusLedCommand);
  setCommand.addArgument("enabled", "must be a boolean", "false");

  return group;
}
