#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "StringHelpers.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiNetwork.h"
#include "wifi/WiFiScanManager.h"
#include "json/Json.h"

#include <string>
#include <string_view>
#include <utility>

const char* const TAG = "SerialCmds::CommandHandlers::WiFi";

// An SSID is 1-32 bytes. A WPA passphrase is 8-63 characters, or 64 hex digits; an open network has none.
static bool isValidSsid(std::string_view ssid)
{
  return !ssid.empty() && ssid.size() <= 32;
}
static bool isValidPassword(std::string_view password)
{
  return password.empty() || (password.size() >= 8 && password.size() <= 64);
}

// Splits "<ssid> [password]". An SSID with spaces is written in double quotes ("My Network" hunter22); the password is
// the rest of the line as given, so it may contain spaces. Returns false on an unterminated quote.
static bool splitSsidAndPassword(std::string_view arg, std::string_view& ssid, std::string_view& password)
{
  if (!arg.empty() && arg.front() == '"') {
    std::size_t end = arg.find('"', 1);
    if (end == std::string_view::npos) {
      return false;
    }
    ssid     = arg.substr(1, end - 1);
    password = OpenShock::StringTrim(arg.substr(end + 1));
    return true;
  }

  auto [first, rest] = OpenShock::StringSplitByFirst(arg, ' ');
  ssid               = first;
  password           = OpenShock::StringTrim(rest);
  return true;
}

// The whole argument is the SSID, optionally in double quotes.
static std::string_view unquoteSsid(std::string_view arg)
{
  if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
    return arg.substr(1, arg.size() - 2);
  }
  return arg;
}

static void writeNetwork(json_gen_str_t* gen, const OpenShock::WiFiNetwork& network)
{
  OpenShock::JSON::objSetString(gen, "ssid", network.ssid);
  OpenShock::JSON::objSetString(gen, "bssid", network.GetHexBSSID().data());
  json_gen_obj_set_int(gen, "channel", network.channel);
  json_gen_obj_set_int(gen, "rssi", network.rssi);
  json_gen_obj_set_int(gen, "authMode", static_cast<int>(network.authMode));
  json_gen_obj_set_bool(gen, "saved", network.IsSaved());
}

static void handleWiFiStatusCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);

  OpenShock::WiFiNetwork network;
  char ip[16];
  if (OpenShock::WiFiManager::GetConnectedNetwork(network) && OpenShock::WiFiManager::GetIPAddress(ip)) {
    json_gen_obj_set_bool(gen, "connected", true);
    writeNetwork(gen, network);
    OpenShock::JSON::objSetString(gen, "ip", ip);
  } else {
    json_gen_obj_set_bool(gen, "connected", false);
  }
  json_gen_obj_set_bool(gen, "scanning", OpenShock::WiFiScanManager::IsScanning());

  json_gen_end_object(gen);
  std::string out = writer.finish();

  SERPR_RESPONSE("WiFiStatus|%s", out.c_str());
}

static void handleWiFiScanCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  if (!OpenShock::WiFiScanManager::StartScan()) {
    SERPR_ERROR("Failed to start WiFi scan (one may already be running)");
    return;
  }

  SERPR_SUCCESS("Scan started, list the results with \"wifi list\"");
}

static void handleWiFiListCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_array(gen);
  for (const auto& network : OpenShock::WiFiManager::GetDiscoveredWiFiNetworks()) {
    json_gen_start_object(gen);
    writeNetwork(gen, network);
    json_gen_end_object(gen);
  }
  json_gen_end_array(gen);
  std::string out = writer.finish();

  SERPR_RESPONSE("WiFiNetworks|%s", out.c_str());
}

static void handleWiFiConnectCommand(std::string_view arg, bool isAutomated)
{
  std::string_view ssidView, passwordView;
  if (!splitSsidAndPassword(arg, ssidView, passwordView)) {
    SERPR_ERROR("Invalid argument (unterminated quote in SSID)");
    return;
  }
  if (!isValidSsid(ssidView)) {
    SERPR_ERROR("Invalid SSID (must be 1-32 characters)");
    return;
  }

  std::string ssid(ssidView);  // Terminated for the WiFiManager API

  if (passwordView.empty()) {
    // No password: connect to a network that is already saved
    if (!OpenShock::WiFiManager::Connect(ssid.c_str())) {
      SERPR_ERROR("Network \"%s\" is not saved; give its password to save it", ssid.c_str());
      return;
    }
    SERPR_SUCCESS("Connecting to \"%s\"", ssid.c_str());
    return;
  }

  if (!isValidPassword(passwordView)) {
    SERPR_ERROR("Invalid password (must be 8-64 characters)");
    return;
  }

  OS_LOGI(TAG, "Saving network \"%s\" and connecting to it", ssid.c_str());
  if (!OpenShock::WiFiManager::Save(ssid.c_str(), passwordView, true)) {
    SERPR_ERROR("Failed to save network \"%s\"", ssid.c_str());
    return;
  }

  SERPR_SUCCESS("Saved \"%s\", connecting", ssid.c_str());
}

static void handleWiFiDisconnectCommand(std::string_view arg, bool isAutomated)
{
  (void)arg;

  if (!OpenShock::WiFiManager::IsConnected()) {
    SERPR_ERROR("Not connected to a network");
    return;
  }

  OpenShock::WiFiManager::Disconnect();

  // The manager reconnects to the best saved network on its own; forgetting a network is what keeps the hub off it
  SERPR_SUCCESS("Disconnected (the hub reconnects to a saved network on its own; use \"wifi forget\" to stay off one)");
}

static void handleWiFiForgetCommand(std::string_view arg, bool isAutomated)
{
  std::string_view ssidView = unquoteSsid(arg);
  if (!isValidSsid(ssidView)) {
    SERPR_ERROR("Invalid SSID (must be 1-32 characters)");
    return;
  }

  std::string ssid(ssidView);
  if (!OpenShock::WiFiManager::Forget(ssid.c_str())) {
    SERPR_ERROR("Failed to forget \"%s\" (not saved?)", ssid.c_str());
    return;
  }

  SERPR_SUCCESS("Forgot \"%s\"", ssid.c_str());
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::WiFiHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("wifi");

  group.addCommand("Get the WiFi connection status.", handleWiFiStatusCommand);
  group.addCommand("status", "Get the WiFi connection status.", handleWiFiStatusCommand);
  group.addCommand("scan", "Start a scan for nearby networks.", handleWiFiScanCommand);
  group.addCommand("list", "List the networks found by the last scans.", handleWiFiListCommand);

  auto& connectCommand = group.addCommand("connect", "Connect to a network, saving it first when a password is given. Put an SSID with spaces in double quotes.", handleWiFiConnectCommand);
  connectCommand.addArgument("ssid [password]", "SSID of 1-32 characters, then the password (8-64 characters) for a network that isn't saved yet", "\"My Network\" hunter22");

  group.addCommand("disconnect", "Disconnect from the current network.", handleWiFiDisconnectCommand);

  auto& forgetCommand = group.addCommand("forget", "Remove a saved network, disconnecting if it is the current one.", handleWiFiForgetCommand);
  forgetCommand.addArgument("ssid", "SSID of a saved network", "\"My Network\"");

  return group;
}
