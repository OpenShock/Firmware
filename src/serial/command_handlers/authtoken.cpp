#include "serial/command_handlers/common.h"

#include "config/Config.h"

#include <string>

void _handleAuthtokenCommand(std::string_view arg) {
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
  return OpenShock::Serial::CommandGroup {
    "authtoken"sv,
    R"(authtoken
  Get the backend auth token.

authtoken [<token>]
  Set the auth token.
  Arguments:
    <token> must be a string.
  Example:
    authtoken mytoken
)",
    _handleAuthtokenCommand,
  };
}
