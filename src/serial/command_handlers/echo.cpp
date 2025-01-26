#include "serial/command_handlers/common.h"
#include "serial/SerialInputHandler.h"

#include "config/Config.h"
#include "Convert.h"
#include "util/StringUtils.h"

static void handleGet(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Get command does not support parameters");
    return;
  }

  // Get current serial echo status
  SERPR_RESPONSE("SerialEcho|%s", OpenShock::SerialInputHandler::SerialEchoEnabled() ? "true" : "false");
}

static void handleSet(std::string_view arg, bool isAutomated)
{
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

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::EchoHandler()
{
  auto group = OpenShock::Serial::CommandGroup("echo"sv);

  auto& getCommand = group.addCommand("Get the serial echo status"sv, handleGet);

  auto& setCommand = group.addCommand("set"sv, "Enable/disable serial echo"sv, handleSet);
  setCommand.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  return group;
}
