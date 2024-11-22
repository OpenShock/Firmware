#include "serial/command_handlers/common.h"

#include "config/Config.h"

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
