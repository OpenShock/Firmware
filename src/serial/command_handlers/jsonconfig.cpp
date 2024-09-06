#include "serial/command_handlers/common.h"

#include "config/Config.h"

void _handleJsonConfigCommand(std::string_view arg) {
  if (arg.empty()) {
    // Get raw config
    std::string json = OpenShock:: Config::GetAsJSON(true);

    SERPR_RESPONSE("JsonConfig|%s", json.c_str());
    return;
  }

  if (!OpenShock:: Config::SaveFromJSON(arg)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  ESP.restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::JsonConfigHandler() {
  auto group = OpenShock::Serial::CommandGroup("jsonconfig"sv);

  auto getter = group.addCommand("Get the configuration as JSON"sv, _handleJsonConfigCommand);

  auto setter = group.addCommand("Set the configuration from JSON, and restart"sv, _handleJsonConfigCommand);
  setter.addArgument("json"sv, "must be a valid JSON object"sv, "{ ... }"sv);
  
  return group;
}
