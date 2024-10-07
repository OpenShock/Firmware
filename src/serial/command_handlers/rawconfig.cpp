#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "util/Base64Utils.h"

#include <vector>

void _handleRawConfigCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    std::vector<uint8_t> buffer;

    // Get raw config
    if (!OpenShock::Config::GetRaw(buffer)) {
      SERPR_ERROR("Failed to get raw config");
      return;
    }

    std::string base64;
    if (!OpenShock::Base64Utils::Encode(buffer.data(), buffer.size(), base64)) {
      SERPR_ERROR("Failed to encode raw config to base64");
      return;
    }

    SERPR_RESPONSE("RawConfig|%s", base64.c_str());
    return;
  }

  std::vector<uint8_t> buffer;
  if (!OpenShock::Base64Utils::Decode(arg.data(), arg.length(), buffer)) {
    SERPR_ERROR("Failed to decode base64");
    return;
  }

  if (!OpenShock::Config::SetRaw(buffer.data(), buffer.size())) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  ESP.restart();
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::RawConfigHandler() {
  auto group = OpenShock::Serial::CommandGroup("rawconfig"sv);

  auto& getCommand = group.addCommand("Get the raw binary config"sv, _handleRawConfigCommand);

  auto& setCommand = group.addCommand("Set the raw binary config, and restart"sv, _handleRawConfigCommand);
  setCommand.addArgument("<base64>"sv, "must be a base64 encoded string"sv, "(base64 encoded binary data)"sv);

  return group;
}
