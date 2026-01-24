#include "serial/command_handlers/common.h"

#include "Chipset.h"

#include <string>

void _handleValidGpiosCommand(std::string_view arg, bool isAutomated) {
  if (!arg.empty()) {
    SERPR_ERROR("Invalid argument (too many arguments)");
    return;
  }

  auto pins = OpenShock::GetValidGPIOPins();

  std::string buffer;
  buffer.reserve(pins.count() * 4);

  for (std::size_t i = 0; i < pins.size(); i++) {
    if (pins[i]) {
      buffer.append(std::to_string(i));
      buffer.append(",");
    }
  }

  if (!buffer.empty()) {
    buffer.pop_back();
  }

  SERPR_RESPONSE("ValidGPIOs|%s", buffer.c_str());
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::ValidGpiosHandler() {
  auto group = OpenShock::SerialCmds::CommandGroup("validgpios"sv);

  auto& cmd = group.addCommand("List all valid GPIO pins"sv, _handleValidGpiosCommand);

  return group;
}
