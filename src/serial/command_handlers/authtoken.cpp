#include "serial/command_handlers/CommandGroup.h"
#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "http/JsonAPI.h"

#include <string>

static void handleGet(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Get command does not support parameters");
    return;
  }

  std::string authToken;
  if (!OpenShock::Config::GetBackendAuthToken(authToken)) {
    SERPR_ERROR("Failed to get auth token from config");
    return;
  }

  // Get auth token
  SERPR_RESPONSE("AuthToken|%s", authToken.c_str());
}

static void handleSet(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_ERROR("Auth token cannot be empty");
    return;
  }

  auto apiResponse = OpenShock::HTTP::JsonAPI::GetHubInfo(arg);
  if (apiResponse.code == 401) {
    SERPR_ERROR("Invalid auth token, refusing to save it!");
    return;
  }

  // If we have some other kind of request fault just set it anyway, we probably arent connected to a network

  bool result = OpenShock::Config::SetBackendAuthToken(arg);

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

static void handleClear(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Clear command does not support parameters");
    return;
  }

  bool result = OpenShock::Config::ClearBackendAuthToken();

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::AuthTokenHandler()
{
  auto group = OpenShock::Serial::CommandGroup("authtoken"sv);

  auto& getCommand = group.addCommand("get"sv, "Get the backend auth token"sv, handleGet);

  auto& setCommand = group.addCommand("set"sv, "Set the backend auth token"sv, handleSet);
  setCommand.addArgument("token"sv, "must be a string"sv, "mytoken"sv);

  auto& clearCommand = group.addCommand("clear"sv, "Clear the backend auth token"sv, handleClear);

  return group;
}
