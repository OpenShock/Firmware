#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "wifi/WiFiManager.h"

#include <cJSON.h>

#include <vector>

const char* const TAG = "SerialCmds::CommandHandlers::Networks";

void _handleNetworksCommand(std::string_view arg, bool isAutomated)
{
  cJSON* root;

  if (arg.empty()) {
    root = cJSON_CreateArray();
    if (root == nullptr) {
      SERPR_ERROR("Failed to create JSON array");
      return;
    }

    if (!OpenShock::Config::GetWiFiCredentials(root, true)) {
      SERPR_ERROR("Failed to get WiFi credentials from config");
      return;
    }

    char* out = cJSON_PrintUnformatted(root);
    if (out == nullptr) {
      SERPR_ERROR("Failed to print JSON");
      return;
    }

    SERPR_RESPONSE("Networks|%s", out);

    cJSON_free(out);
    return;
  }

  root = cJSON_ParseWithLength(arg.data(), arg.length());
  if (root == nullptr) {
    SERPR_ERROR("Failed to parse JSON: %s", cJSON_GetErrorPtr());
    return;
  }

  if (cJSON_IsArray(root) == 0) {
    SERPR_ERROR("Invalid argument (not an array)");
    return;
  }

  std::vector<OpenShock::Config::WiFiCredentials> creds;

  uint8_t id     = 1;
  cJSON* network = nullptr;
  cJSON_ArrayForEach(network, root)
  {
    OpenShock::Config::WiFiCredentials cred;

    if (!cred.FromJSON(network)) {
      SERPR_ERROR("Failed to parse network");
      return;
    }

    if (cred.id == 0) {
      cred.id = id++;
    }

    OS_LOGI(TAG, "Adding network \"%s\" to config, id=%u", cred.ssid.c_str(), cred.id);

    creds.push_back(std::move(cred));
  }

  if (!OpenShock::Config::SetWiFiCredentials(creds)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");

  OpenShock::WiFiManager::RefreshNetworkCredentials();
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::NetworksHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("networks"sv);

  auto& getCommand = group.addCommand("Get all saved networks."sv, _handleNetworksCommand);

  auto& setCommand = group.addCommand("Set all saved networks."sv, _handleNetworksCommand);
  setCommand.addArgument(
    "json"sv,
    "must be a array of objects with the following fields:"sv,
    "[{\"ssid\":\"myssid\",\"password\":\"mypassword\"}]"sv,
    {
      "ssid     (string)  SSID of the network"sv,
      "password (string)  Password of the network"sv,
      "id       (number)  ID of the network (optional)"sv,
    }
  );

  return group;
}
