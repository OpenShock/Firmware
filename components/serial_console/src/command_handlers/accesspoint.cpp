#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "StringHelpers.h"
#include "wifi/WiFiConfig.h"

#include <string>

static void handleAccessPointGetCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  std::string ssid     = OpenShock::Config::GetWiFiAccessPointSSID();
  std::string password = OpenShock::Config::GetWiFiAccessPointPassword();
  bool isDefault       = ssid == CONFIG_OPENSHOCK_FW_AP_PREFIX;

  const char* protection = "custom-password";
  if (password.empty()) {
    protection = "open";
  } else if (password == CONFIG_OPENSHOCK_FW_AP_PASSWORD) {
    protection = "default-password";
  }

  // A default SSID is shown as the prefix; the hub appends its MAC to it when the access point starts
  SERPR_RESPONSE("AccessPoint|%s|%s|%s", ssid.c_str(), isDefault ? "default" : "custom", protection);
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
  // "default" brings back the build's default password; it is too short to be a password itself
  std::string password = OpenShock::StringIEquals(arg, "default") ? std::string(CONFIG_OPENSHOCK_FW_AP_PASSWORD) : std::string(arg);
  if (!OpenShock::Config::IsValidAccessPointPassword(password) || arg.empty()) {
    SERPR_ERROR("Invalid password (must be 8-63 characters; use \"ap open\" to remove it)");
    return;
  }

  if (!OpenShock::Config::SetWiFiAccessPointPassword(std::move(password))) {
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

  group.addCommand("Get the captive portal's access point name and whether it is open or uses the default or a custom password.", handleAccessPointGetCommand);

  auto& ssidCommand = group.addCommand("ssid", "Set the access point name; \"default\" restores the name with the hub's MAC.", handleAccessPointSsidCommand);
  ssidCommand.addArgument("ssid", "1-32 characters, or default", "MyHub");

  auto& passwordCommand = group.addCommand("password", "Set the access point's WPA2/WPA3 password; \"default\" restores the build's default, as a factory reset does.", handleAccessPointPasswordCommand);
  passwordCommand.addArgument("password", "8-63 characters, or default", "hunter2hunter2");

  group.addCommand("open", "Remove the access point password.", handleAccessPointOpenCommand);

  return group;
}
