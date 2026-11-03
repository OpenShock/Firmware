#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "wifi/WiFiConfig.h"
#include "wifi/WiFiManager.h"
#include "json/Json.h"

#include <vector>

const char* const TAG = "SerialCmds::CommandHandlers::Networks";

static void handleNetworksCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    OpenShock::JSON::StringWriter writer;
    json_gen_str_t* gen = writer.gen();

    json_gen_start_array(gen);
    OpenShock::Config::GetWiFiCredentials(gen, true);
    json_gen_end_array(gen);

    std::string out = writer.finish();

    SERPR_RESPONSE("Networks|%s", out.c_str());
    return;
  }

  OpenShock::JSON::JsonDocument doc;
  if (!doc.parse(arg)) {
    SERPR_ERROR("Failed to parse JSON");
    return;
  }

  OpenShock::JSON::JsonView root = doc.root();
  if (!root.isArray()) {
    SERPR_ERROR("Invalid argument (not an array)");
    return;
  }

  // A network without an id (0) gets the lowest free one from the config section itself
  std::vector<OpenShock::Config::WiFiCredentials> creds;
  const int count = root.count();
  for (int i = 0; i < count; ++i) {
    OpenShock::Config::WiFiCredentials cred;
    if (!cred.FromJSON(root.at(i))) {
      SERPR_ERROR("Failed to parse network");
      return;
    }
    OS_LOGI(TAG, "Adding network \"%s\" to config", cred.ssid.c_str());
    creds.push_back(std::move(cred));
  }

  const bool saved = OpenShock::Config::SetWiFiCredentials(creds);
  OpenShock::SerialCmds::ReportSaved(saved);
  if (saved) {
    OpenShock::WiFiManager::RefreshNetworkCredentials();
  }
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::NetworksHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("networks");

  group.addCommand("Get all saved networks.", handleNetworksCommand);

  static constexpr std::string_view kJsonFields[] = {
    "ssid     (string)  SSID of the network",
    "password (string)  Password of the network",
    "id       (number)  ID of the network (optional)",
  };

  auto& setCommand = group.addCommand("Set all saved networks.", handleNetworksCommand);
  setCommand.addArgument("json", "must be a array of objects with the following fields:", "[{\"ssid\":\"myssid\",\"password\":\"mypassword\"}]", kJsonFields);

  return group;
}
