#include "serial/command_handlers/common.h"

#include "config/Config.h"

static void handleGet(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Get command does not support parameters");
    return;
  }

  // Get raw config
  std::string json = OpenShock::Config::GetAsJSON(true);

  SERPR_RESPONSE("JsonConfig|%s", json.c_str());
}

static void handleSet(std::string_view arg, bool isAutomated)
{
  if (!OpenShock::Config::SaveFromJSON(arg)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  ESP.restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::JsonConfigHandler()
{
  auto group = OpenShock::Serial::CommandGroup("jsonconfig"sv);

  auto& getCommand = group.addCommand("Get the configuration as JSON"sv, handleGet);

  auto& setCommand = group.addCommand("set"sv, "Set the configuration from JSON, and restart"sv, handleSet);
  setCommand.addArgument("json"sv, "must be a valid JSON object"sv, "{ ... }"sv);

  return group;
}
