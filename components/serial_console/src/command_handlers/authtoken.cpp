#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "BackendConfig.h"
#include "GatewayConnectionManager.h"
#include "http/JsonAPI.h"

#include <string>
#include <utility>

static void handleAuthtokenCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("AuthToken|%s", OpenShock::Config::GetBackendAuthToken().c_str());
    return;
  }

  // One copy: terminated for the request, then moved into the config
  std::string token(arg);

  auto apiResponse = OpenShock::HTTP::JsonAPI::GetHubInfo(token.c_str());
  if (apiResponse.code == 401) {
    SERPR_ERROR("Invalid auth token, refusing to save it!");
    return;
  }

  // If we have some other kind of request fault just set it anyway, we probably arent connected to a network

  OpenShock::SerialCmds::ReportSaved(OpenShock::GatewayConnectionManager::SetAuthToken(std::move(token)));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::AuthTokenHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("authtoken");

  group.addCommand("Get the backend auth token", handleAuthtokenCommand);

  auto& setCommand = group.addCommand("Set the auth token", handleAuthtokenCommand);
  setCommand.addArgument("token", "must be a string", "mytoken");

  return group;
}
