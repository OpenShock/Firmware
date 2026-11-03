#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "StringHelpers.h"
#include "wifi/WiFiConfig.h"

#include <string>

static void handleAccessPointGetCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  std::string ssid = OpenShock::Config::GetWiFiAccessPointSSID();
  bool isDefault   = ssid == CONFIG_OPENSHOCK_FW_AP_PREFIX;
  bool hasPassword = !OpenShock::Config::GetWiFiAccessPointPassword().empty();

  // A default SSID is shown as the prefix; the hub appends its MAC to it when the access point starts
  SERPR_RESPONSE("AccessPoint|%s|%s|%s", ssid.c_str(), isDefault ? "default" : "custom", hasPassword ? "protected" : "open");
}

static void handleAccessPointSsidCommand(std::string_view arg, bool isAutomated)
{
  // "default" brings back the prefix + MAC name
  std::string ssid = OpenShock::StringIEquals(arg, "default") ? std::string(CONFIG_OPENSHOCK_FW_AP_PREFIX) : std::string(arg);
  if (!OpenShock::Config::IsValidAccessPointSSID(ssid)) {
    SERPR_ERROR("Invalid SSID (must be 1-32 characters)");
    return;
  }

  if (!OpenShock::Config::SetWiFiAccessPointSSID(std::move(ssid))) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, applies the next time the captive portal starts");
}

static void handleAccessPointPasswordCommand(std::string_view arg, bool isAutomated)
{
  if (!OpenShock::Config::IsValidAccessPointPassword(arg) || arg.empty()) {
    SERPR_ERROR("Invalid password (must be 8-63 characters; use \"ap open\" to remove it)");
    return;
  }

  if (!OpenShock::Config::SetWiFiAccessPointPassword(std::string(arg))) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, applies the next time the captive portal starts");
}

static void handleAccessPointOpenCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  if (!OpenShock::Config::SetWiFiAccessPointPassword({})) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, the access point is open from the next time the captive portal starts");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::AccessPointHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("ap");

  group.addCommand("Get the captive portal's access point name and whether it has a password.", handleAccessPointGetCommand);

  auto& ssidCommand = group.addCommand("ssid", "Set the access point name; \"default\" restores the name with the hub's MAC.", handleAccessPointSsidCommand);
  ssidCommand.addArgument("ssid", "1-32 characters, or default", "MyHub");

  auto& passwordCommand = group.addCommand("password", "Protect the access point with a WPA2 password. A factory reset removes it.", handleAccessPointPasswordCommand);
  passwordCommand.addArgument("password", "8-63 characters", "hunter2hunter2");

  group.addCommand("open", "Remove the access point password.", handleAccessPointOpenCommand);

  return group;
}
