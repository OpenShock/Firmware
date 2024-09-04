#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "config/Config.h"

void _handleFactoryResetCommand(OpenShock::StringView arg) {
  (void)arg;

  Serial.println("Resetting to factory defaults...");
  OpenShock::Config::FactoryReset();
  Serial.println("Restarting...");
  ESP.restart();
}

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::FactoryResetHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "factoryreset"_sv,
    R"(factoryreset
  Reset the device to factory defaults and restart
  Example:
    factoryreset
)",
    _handleFactoryResetCommand,
  };
}
