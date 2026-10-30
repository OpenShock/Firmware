#include "serial_console/command_handlers/common.h"

#include "config/Config.h"
#include "util/HostnameUtils.h"

#include <esp_system.h>

#include <string>

const char* const TAG = "SerialCmds::CommandHandlers::Hostname";

static void handleHostnameCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    auto hostname = OpenShock::Config::GetWiFiHostname();
    if (!hostname) {
      SERPR_ERROR("Failed to get hostname from config");
      return;
    }
    // Get hostname
    SERPR_RESPONSE("Hostname|%s", hostname->c_str());
    return;
  }

  if (!OpenShock::HostnameUtils::IsValid(arg)) {
    SERPR_ERROR("Invalid hostname: use 1-32 letters, digits or hyphens, not starting or ending with a hyphen");
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

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::HostnameHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("hostname"sv);

  group.addCommand("Get the network hostname."sv, handleHostnameCommand);

  auto& setCommand = group.addCommand("Set the network hostname."sv, handleHostnameCommand);
  setCommand.addArgument("hostname"sv, "letters, digits and hyphens, max 32 characters"sv, std::string_view(CONFIG_OPENSHOCK_FW_HOSTNAME));

  return group;
}
