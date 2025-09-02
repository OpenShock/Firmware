#include "serial/command_handlers/CommandGroup.h"
#include "serial/command_handlers/common.h"

#include "config/Config.h"

#include <esp_system.h>

#include <string>

const char* const TAG = "Serial::CommandHandlers::Domain";

static void handeGet(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Get command does not support parameters");
    return;
  }

  std::string hostname;
  if (!OpenShock::Config::GetWiFiHostname(hostname)) {
    SERPR_ERROR("Failed to get hostname from config");
    return;
  }
  // Get hostname
  SERPR_RESPONSE("Hostname|%s", hostname.c_str());
}

static void handleSet(std::string_view arg, bool isAutomated)
{
  bool result = OpenShock::Config::SetWiFiHostname(arg);
  if (result) {
    SERPR_SUCCESS("Saved config, restarting...");
    esp_restart();
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::HostnameHandler()
{
  auto group = OpenShock::Serial::CommandGroup("hostname"sv);

  auto& getCommand = group.addCommand("Get the network hostname."sv, handeGet);

  auto& setCommand = group.addCommand("set"sv, "Set the network hostname."sv, handleSet);
  setCommand.addArgument("hostname"sv, "must be a string"sv, "OpenShock"sv);

  return group;
}
