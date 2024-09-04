#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/CommandEntry.h"
#include "serial/command_handlers/impl/common.h"

#include "Chipset.h"

#include <string>

void _handleValidGpiosCommand(OpenShock::StringView arg) {
  if (!arg.isNullOrEmpty()) {
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

std::vector<OpenShock::Serial::CommandHandlers::CommandEntry> OpenShock::Serial::CommandHandlers::ValidGpiosHandler() {
  return OpenShock::Serial::CommandHandlers::CommandEntry("validgpios"_sv, "List all valid GPIO pins"_sv, _handleValidGpiosCommand);
}
