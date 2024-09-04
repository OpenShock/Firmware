#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "config/Config.h"
#include "StringView.h"

#include <string>

void _handleAuthtokenCommand(OpenShock::StringView arg) {
  if (arg.isNullOrEmpty()) {
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

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::AuthTokenHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "authtoken"_sv,
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
