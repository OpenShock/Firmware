#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "BackendConfig.h"
#include "GatewayConnectionManager.h"
#include "http/JsonAPI.h"

#include <string>

static void handleAuthtokenCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("AuthToken|%s", OpenShock::Config::GetBackendAuthToken().c_str());
    return;
  }

  auto apiResponse = OpenShock::HTTP::JsonAPI::GetHubInfo(arg);
  if (apiResponse.code == 401) {
    SERPR_ERROR("Invalid auth token, refusing to save it!");
    return;
  }

  // If we have some other kind of request fault just set it anyway, we probably arent connected to a network

  OpenShock::SerialCmds::ReportSaved(OpenShock::GatewayConnectionManager::SetAuthToken(std::string(arg)));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::AuthTokenHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("authtoken"sv);

  group.addCommand("Get the backend auth token"sv, handleAuthtokenCommand);

  auto& setCommand = group.addCommand("Set the auth token"sv, handleAuthtokenCommand);
  setCommand.addArgument("token"sv, "must be a string"sv, "mytoken"sv);

  return group;
}
