#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "util/HostnameUtils.h"
#include "wifi/WiFiConfig.h"

#include <string>

const char* const TAG = "SerialCmds::CommandHandlers::Hostname";

static void handleHostnameCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("Hostname|%s", OpenShock::Config::GetWiFiHostname().c_str());
    return;
  }

  if (!OpenShock::HostnameUtils::IsValid(arg)) {
    SERPR_ERROR("Invalid hostname: use 1-32 letters, digits or hyphens, not starting or ending with a hyphen");
    return;
  }

  OpenShock::SerialCmds::ReportSavedAndRestart(OpenShock::Config::SetWiFiHostname(std::string(arg)));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::HostnameHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("hostname"sv);

  group.addCommand("Get the network hostname."sv, handleHostnameCommand);

  auto& setCommand = group.addCommand("Set the network hostname."sv, handleHostnameCommand);
  setCommand.addArgument("hostname"sv, "letters, digits and hyphens, max 32 characters"sv, std::string_view(CONFIG_OPENSHOCK_FW_HOSTNAME));

  return group;
}
