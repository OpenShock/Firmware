#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "Convert.h"
#include "serial_console/SerialInputConfig.h"
#include "StringHelpers.h"

static void handleSerialEchoCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("SerialEcho|%s", OpenShock::Config::GetSerialInputConfigEchoEnabled() ? "true" : "false");
    return;
  }

  auto enabled = OpenShock::Convert::ToBool(OpenShock::StringTrim(arg));
  if (!enabled) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  OpenShock::SerialCmds::ReportSaved(OpenShock::Config::SetSerialInputConfigEchoEnabled(*enabled));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::EchoHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("echo");

  group.addCommand("Get the serial echo status", handleSerialEchoCommand);

  auto& setCommand = group.addCommand("Enable/disable serial echo", handleSerialEchoCommand);
  setCommand.addArgument("enabled", "must be a boolean", "true");

  return group;
}
