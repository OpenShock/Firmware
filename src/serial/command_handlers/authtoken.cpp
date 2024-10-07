#include "serial/command_handlers/common.h"

#include "config/Config.h"

#include <string>

void _handleAuthtokenCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    std::string authToken;
    if (!OpenShock::Config::GetBackendAuthToken(authToken)) {
      SERPR_ERROR("Failed to get auth token from config");
      return;
    }

    // Get auth token
    SERPR_RESPONSE("AuthToken|%s", authToken.c_str());
    return;
  }

  bool result = OpenShock::Config::SetBackendAuthToken(arg);

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::AuthTokenHandler() {
  auto group = OpenShock::Serial::CommandGroup("authtoken"sv);

  auto& getCommand = group.addCommand("Get the backend auth token"sv, _handleAuthtokenCommand);

  auto& setCommand = group.addCommand("Set the auth token"sv, _handleAuthtokenCommand);
  setCommand.addArgument("<token>"sv, "must be a string"sv, "mytoken"sv);

  return group;
}
