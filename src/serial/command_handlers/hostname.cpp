#include "serial/command_handlers/common.h"

#include "config/Config.h"

#include <string>

const char* const TAG = "Serial::CommandHandlers::Domain";

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

  bool result = OpenShock::Config::SetWiFiHostname(arg);
  if (result) {
    SERPR_SUCCESS("Saved config, restarting...");
    ESP.restart();
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::HostnameHandler() {
  auto group = OpenShock::Serial::CommandGroup("hostname"sv);

  auto getCommand = group.addCommand("Get the network hostname."sv, _handleHostnameCommand);

  auto setCommand = group.addCommand("Set the network hostname."sv, _handleHostnameCommand);
  setCommand.addArgument("hostname"sv, "must be a string"sv, "OpenShock"sv);

  return group;
}
