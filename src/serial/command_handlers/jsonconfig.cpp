#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "config/Config.h"

void _handleJsonConfigCommand(OpenShock::StringView arg) {
  if (arg.isNullOrEmpty()) {
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

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::JsonConfigHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "jsonconfig"_sv,
    R"(jsonconfig
  Get the configuration as JSON
  Example:
    jsonconfig

jsonconfig <json>
  Set the configuration from JSON, and restart
  Arguments:
    <json> must be a valid JSON object
  Example:
    jsonconfig { ... }
)",
    _handleJsonConfigCommand,
  };
}
