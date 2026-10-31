#include "serial_console/command_handlers/common.h"

#include "Base64.h"
#include "config/Wire.h"
#include "TinyVec.h"

#include <vector>

static void handleRawConfigCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    TinyVec<uint8_t> buffer;

    // Get raw config
    if (!OpenShock::Config::GetRaw(buffer)) {
      SERPR_ERROR("Failed to get raw config");
      return;
    }

    std::string base64;
    if (!OpenShock::Base64::Encode(buffer, base64)) {
      SERPR_ERROR("Failed to encode raw config to base64");
      return;
    }

    SERPR_RESPONSE("RawConfig|%s", base64.c_str());
    return;
  }

  TinyVec<uint8_t> buffer;
  if (!OpenShock::Base64::Decode(arg, buffer)) {
    SERPR_ERROR("Failed to decode base64");
    return;
  }

  OpenShock::SerialCmds::ReportSavedAndRestart(OpenShock::Config::SetRaw(buffer.data(), buffer.size()));
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::RawConfigHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("rawconfig"sv);

  group.addCommand("Get the raw binary config"sv, handleRawConfigCommand);

  auto& setCommand = group.addCommand("Set the raw binary config, and restart"sv, handleRawConfigCommand);
  setCommand.addArgument("base64"sv, "must be a base64 encoded string"sv, "(base64 encoded binary data)"sv);

  return group;
}
