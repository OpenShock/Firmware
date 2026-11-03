#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "CommandHandler.h"
#include "Convert.h"
#include "radio/RFConfig.h"
#include "StringHelpers.h"

static void handleKeepAliveCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("KeepAlive|%s", OpenShock::Config::GetRFConfigKeepAliveEnabled() ? "true" : "false");
    return;
  }

  auto keepAliveEnabled = OpenShock::Convert::ToBool(OpenShock::StringTrim(arg));
  if (!keepAliveEnabled) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  OpenShock::SerialCmds::ReportSaved(OpenShock::CommandHandler::SetKeepAliveEnabled(*keepAliveEnabled));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::KeepAliveHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("keepalive"sv);

  group.addCommand("Get the shocker keep-alive status"sv, handleKeepAliveCommand);

  auto& setCommand = group.addCommand("Enable/disable shocker keep-alive"sv, handleKeepAliveCommand);
  setCommand.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  return group;
}
