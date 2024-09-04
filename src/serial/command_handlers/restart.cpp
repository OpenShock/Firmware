#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/SerialCmdHandler.h"

void _handleRestartCommand(OpenShock::StringView arg) {
  (void)arg;

  Serial.println("Restarting ESP...");
  ESP.restart();
}

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::RestartHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "restart"_sv,
    R"(restart
  Restart the board
  Example:
    restart
)",
    _handleRestartCommand,
  };
}
