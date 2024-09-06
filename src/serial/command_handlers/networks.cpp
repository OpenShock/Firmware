#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "wifi/WiFiManager.h"

#include <cJSON.h>

#include <vector>

const char* const TAG = "Serial::CommandHandlers::Networks";

void _handleNetworksCommand(std::string_view arg) {
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
  cJSON_ArrayForEach(network, root) {
    OpenShock::Config::WiFiCredentials cred;

    if (!cred.FromJSON(network)) {
      SERPR_ERROR("Failed to parse network");
      return;
    }

    if (cred.id == 0) {
      cred.id = id++;
    }

    OS_LOGI(TAG, "Adding network \"%s\" to config, id=%u", cred.ssid.c_str(), cred.id);

    creds.emplace_back(std::move(cred));
  }

  if (!OpenShock::Config::SetWiFiCredentials(creds)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");

  OpenShock::WiFiManager::RefreshNetworkCredentials();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::NetworksHandler() {
  auto group = OpenShock::Serial::CommandGroup("networks"sv);

  auto getter = group.addCommand("Get all saved networks."sv, _handleNetworksCommand);

  auto setter = group.addCommand("Set all saved networks."sv, _handleNetworksCommand);
  setter.addArgument("json"sv, "must be a array of objects with the following fields:"sv, "[{\"ssid\":\"myssid\",\"password\":\"mypassword\"}]"sv);

  return group;
}

/*
  return OpenShock::Serial::CommandGroup {
    "networks"sv,
    R"(networks
  Get all saved networks.

networks [<json>]
  Set all saved networks.
  Arguments:
    <json> must be a array of objects with the following fields:
      ssid     (string)  SSID of the network
      password (string)  Password of the network
      id       (number)  ID of the network (optional)
  Example:
    networks [{\"ssid\":\"myssid\",\"password\":\"mypassword\"}]
)",
    _handleNetworksCommand,
  };
*/