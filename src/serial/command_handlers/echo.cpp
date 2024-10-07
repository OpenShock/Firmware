#include "serial/command_handlers/common.h"

#include "serial/SerialInputHandler.h"

#include "config/Config.h"
#include "Convert.h"
#include "util/StringUtils.h"

void _handleSerialEchoCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    // Get current serial echo status
    SERPR_RESPONSE("SerialEcho|%s", OpenShock::SerialInputHandler::SerialEchoEnabled() ? "true" : "false");
    return;
  }

  bool enabled;
  if (!OpenShock::Convert::ToBool(OpenShock::StringTrim(arg), enabled)) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  bool result = OpenShock::Config::SetSerialInputConfigEchoEnabled(enabled);
  OpenShock::SerialInputHandler::SetSerialEchoEnabled(enabled);

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::EchoHandler() {
  auto group = OpenShock::Serial::CommandGroup("echo"sv);

  auto& getCommand = group.addCommand("Get the serial echo status"sv, _handleSerialEchoCommand);

  auto& setCommand = group.addCommand("Enable/disable serial echo"sv, _handleSerialEchoCommand);
  setCommand.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  return group;
}
