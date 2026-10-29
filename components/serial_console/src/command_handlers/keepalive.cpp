#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "CommandHandler.h"
#include "Convert.h"
#include "radio/RFConfig.h"
#include "StringHelpers.h"

static void handleKeepAliveCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    // Get keep alive status
    auto configEnabled = OpenShock::Config::GetRFConfigKeepAliveEnabled();
    if (!configEnabled) {
      SERPR_ERROR("Failed to get keep-alive status from config");
      return;
    }

    SERPR_RESPONSE("KeepAlive|%s", *configEnabled ? "true" : "false");
    return;
  }

  auto keepAliveEnabled = OpenShock::Convert::ToBool(OpenShock::StringTrim(arg));
  if (!keepAliveEnabled) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  bool result = OpenShock::CommandHandler::SetKeepAliveEnabled(*keepAliveEnabled);

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::KeepAliveHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("keepalive"sv);

  group.addCommand("Get the shocker keep-alive status"sv, handleKeepAliveCommand);

  auto& setCommand = group.addCommand("Enable/disable shocker keep-alive"sv, handleKeepAliveCommand);
  setCommand.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  return group;
}
