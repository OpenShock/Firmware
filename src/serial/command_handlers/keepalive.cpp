#include "serial/command_handlers/common.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "Convert.h"
#include "util/StringUtils.h"

static void handleGet(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Get command does not support parameters");
    return;
  }

  bool keepAliveEnabled;
  if (!OpenShock::Config::GetRFConfigKeepAliveEnabled(keepAliveEnabled)) {
    SERPR_ERROR("Failed to get keep-alive status from config");
    return;
  }

  SERPR_RESPONSE("KeepAlive|%s", keepAliveEnabled ? "true" : "false");
}

static void handleSet(std::string_view arg, bool isAutomated)
{
  bool keepAliveEnabled;
  if (!OpenShock::Convert::ToBool(OpenShock::StringTrim(arg), keepAliveEnabled)) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  bool result = OpenShock::CommandHandler::SetKeepAliveEnabled(keepAliveEnabled);

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::KeepAliveHandler()
{
  auto group = OpenShock::Serial::CommandGroup("keepalive"sv);

  auto& getCommand = group.addCommand("get"sv, "Get the shocker keep-alive status"sv, handleGet);

  auto& setCommand = group.addCommand("set"sv, "Enable/disable shocker keep-alive"sv, handleSet);
  setCommand.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  return group;
}
