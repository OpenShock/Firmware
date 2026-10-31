#include "serial_console/command_handlers/common.h"

#include "config/Store.h"

static void handleJsonConfigCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    // Get raw config
    std::string json = OpenShock::Config::GetAsJSON(true);

    SERPR_RESPONSE("JsonConfig|%s", json.c_str());
    return;
  }

  OpenShock::SerialCmds::ReportSavedAndRestart(OpenShock::Config::SaveFromJSON(arg));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::JsonConfigHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("jsonconfig"sv);

  group.addCommand("Get the configuration as JSON"sv, handleJsonConfigCommand);

  auto& setCommand = group.addCommand("Set the configuration from JSON, and restart"sv, handleJsonConfigCommand);
  setCommand.addArgument("json"sv, "must be a valid JSON object"sv, "{ ... }"sv);

  return group;
}
