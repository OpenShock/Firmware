#include "serial/command_handlers/common.h"

#include "config/Config.h"

#include <esp_system.h>

#include <string>

const char* const TAG = "SerialCmds::CommandHandlers::Domain";

void _handleHostnameCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    std::string hostname;
    if (!OpenShock::Config::GetWiFiHostname(hostname)) {
      SERPR_ERROR("Failed to get hostname from config");
      return;
    }
    // Get hostname
    SERPR_RESPONSE("Hostname|%s", hostname.c_str());
    return;
  }

  bool result = OpenShock::Config::SetWiFiHostname(std::string(arg));
  if (result) {
    SERPR_SUCCESS("Saved config, restarting...");
    esp_restart();
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::HostnameHandler() {
  auto group = OpenShock::SerialCmds::CommandGroup("hostname"sv);

  auto& getCommand = group.addCommand("Get the network hostname."sv, _handleHostnameCommand);

  auto& setCommand = group.addCommand("Set the network hostname."sv, _handleHostnameCommand);
  setCommand.addArgument("hostname"sv, "must be a string"sv, "OpenShock"sv);

  return group;
}
