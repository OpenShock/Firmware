#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/CommandEntry.h"

#include "SerialInputHandler.h"

#include <vector>

void _handleVersionCommand(OpenShock::StringView arg) {
  (void)arg;

  Serial.print("\n");
  SerialInputHandler::PrintVersionInfo();
}

std::vector<OpenShock::Serial::CommandHandlers::CommandEntry> OpenShock::Serial::CommandHandlers::VersionHandler() {
  return {OpenShock::Serial::CommandHandlers::CommandEntry("version"_sv, "Print version information"_sv, _handleVersionCommand)};
}
