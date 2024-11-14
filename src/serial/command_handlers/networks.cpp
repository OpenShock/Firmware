#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "wifi/WiFiManager.h"

#include <cJSON.h>

#include <vector>

const char* const TAG = "Serial::CommandHandlers::Networks";

static void handleGet(std::string_view arg, bool isAutomated)
{
  cJSON* root = cJSON_CreateArray();
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
}

static void handleSet(std::string_view arg, bool isAutomated)
{
  cJSON* root = cJSON_ParseWithLength(arg.data(), arg.length());
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

    creds.emplace_back(std::move(cred));
  }

  if (!OpenShock::Config::SetWiFiCredentials(creds)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");

  OpenShock::WiFiManager::RefreshNetworkCredentials();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::NetworksHandler()
{
  auto group = OpenShock::Serial::CommandGroup("networks"sv);

  auto& getCommand = group.addCommand("get"sv, "Get all saved networks."sv, handleGet);

  auto& setCommand = group.addCommand("set"sv, "Set all saved networks."sv, handleSet);
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
