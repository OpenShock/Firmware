#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "config/Config.h"
#include "wifi/WiFiManager.h"

#include <cJSON.h>

#include <vector>

void _handleNetworksCommand(OpenShock::StringView arg) {
  cJSON* root;

  if (arg.isNullOrEmpty()) {
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

  std::vector<Config::WiFiCredentials> creds;

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

    ESP_LOGI(TAG, "Adding network \"%s\" to config, id=%u", cred.ssid.c_str(), cred.id);

    creds.emplace_back(std::move(cred));
  }

  if (!OpenShock::Config::SetWiFiCredentials(creds)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");

  OpenShock::WiFiManager::RefreshNetworkCredentials();
}

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::NetworksHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "networks"_sv,
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
}
