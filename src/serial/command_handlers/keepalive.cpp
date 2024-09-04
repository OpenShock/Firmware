#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "CommandHandler.h"
#include "config/Config.h"

void _handleKeepAliveCommand(OpenShock::StringView arg) {
  bool keepAliveEnabled;

  if (arg.isNullOrEmpty()) {
    // Get keep alive status
    if (!OpenShock::Config::GetRFConfigKeepAliveEnabled(keepAliveEnabled)) {
      SERPR_ERROR("Failed to get keep-alive status from config");
      return;
    }

    SERPR_RESPONSE("KeepAlive|%s", keepAliveEnabled ? "true" : "false");
    return;
  }

  if (!_tryParseBool(arg, keepAliveEnabled)) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  bool result = OpenShock::CommandHandler::SetKeepAliveEnabled(keepAliveEnabled);

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::KeepAliveHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "keepalive"_sv,
    R"(keepalive
  Get the shocker keep-alive status.

keepalive [<bool>]
  Enable/disable shocker keep-alive.
  Arguments:
    <bool> must be a boolean.
  Example:
    keepalive true
)",
    _handleKeepAliveCommand,
  };
}
