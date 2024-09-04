#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "config/Config.h"

void _handleSerialEchoCommand(OpenShock::StringView arg) {
  if (arg.isNullOrEmpty()) {
    // Get current serial echo status
    SERPR_RESPONSE("SerialEcho|%s", s_echoEnabled ? "true" : "false");
    return;
  }

  bool enabled;
  if (!_tryParseBool(arg, enabled)) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  bool result   = OpenShock::Config::SetSerialInputConfigEchoEnabled(enabled);
  s_echoEnabled = enabled;

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::EchoHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "echo"_sv,
    R"(echo
  Get the serial echo status.
  If enabled, typed characters are echoed back to the serial port.

echo [<bool>]
  Enable/disable serial echo.
  Arguments:
    <bool> must be a boolean.
  Example:
    echo true
)",
    _handleSerialEchoCommand,
  };
}
