#include "serial/command_handlers/common.h"

#include "serial/SerialInputHandler.h"

#include <vector>

void _handleVersionCommand(std::string_view arg) {
  (void)arg;

  ::Serial.print("\n");
  SerialInputHandler::PrintVersionInfo();
}

std::vector<OpenShock::Serial::CommandHandlers::CommandEntry> OpenShock::Serial::CommandHandlers::VersionHandler() {
  return {OpenShock::Serial::CommandHandlers::CommandEntry("version"sv, "Print version information"sv, _handleVersionCommand)};
}
