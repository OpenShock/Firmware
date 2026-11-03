#include "serial_console/command_handlers/common.h"

#include "Chipset.h"

#include <charconv>

static void handleValidGpiosCommand(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Invalid argument (too many arguments)");
    return;
  }

  auto pins = OpenShock::GetValidGPIOPins();

  // At most 3 digits and a comma per pin
  char buffer[decltype(pins) {}.size() * 4];
  char* end = buffer;

  for (std::size_t i = 0; i < pins.size(); i++) {
    if (pins[i]) {
      if (end != buffer) {
        *end++ = ',';
      }
      end = std::to_chars(end, buffer + sizeof(buffer), i).ptr;
    }
  }

  SERPR_RESPONSE("ValidGPIOs|%.*s", static_cast<int>(end - buffer), buffer);
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::ValidGpiosHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("validgpios");

  group.addCommand("List all valid GPIO pins", handleValidGpiosCommand);

  return group;
}
