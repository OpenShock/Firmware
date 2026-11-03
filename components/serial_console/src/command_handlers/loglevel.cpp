#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "Logging.h"
#include "StringHelpers.h"

#include <cstdint>
#include <optional>
#include <string_view>

static constexpr std::string_view kLevelNames[] = {"none", "error", "warn", "info", "debug", "verbose"};

static std::string_view levelName(uint8_t level)
{
  return level < std::size(kLevelNames) ? kLevelNames[level] : "unknown";
}

static std::optional<uint8_t> parseLevel(std::string_view arg)
{
  for (uint8_t i = 0; i < std::size(kLevelNames); ++i) {
    if (OpenShock::StringIEquals(arg, kLevelNames[i])) {
      return i;
    }
  }
  if (arg.size() == 1 && arg[0] >= '0' && arg[0] <= '5') {
    return static_cast<uint8_t>(arg[0] - '0');
  }
  return std::nullopt;
}

static void handleLogLevelCommand(std::string_view arg, bool isAutomated)
{
  const std::string_view compiled = levelName(OpenShock::Log::kCompiledLevel);

  if (arg.empty()) {
    const std::string_view current = levelName(OpenShock::Log::GetRuntimeLevel());
    SERPR_RESPONSE("LogLevel|%.*s|%.*s", static_cast<int>(current.size()), current.data(), static_cast<int>(compiled.size()), compiled.data());
    return;
  }

  auto level = parseLevel(arg);
  if (!level) {
    SERPR_ERROR("Invalid argument (must be none, error, warn, info, debug or verbose)");
    return;
  }

  // Levels above the compiled one are not in the image, so they can't be turned on at runtime
  if (*level > OpenShock::Log::kCompiledLevel) {
    SERPR_ERROR("This build only includes logs up to %.*s", static_cast<int>(compiled.size()), compiled.data());
    return;
  }

  const std::string_view applied = levelName(OpenShock::Log::SetRuntimeLevel(*level));
  SERPR_SUCCESS("Log level set to %.*s until restart", static_cast<int>(applied.size()), applied.data());
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::LogLevelHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("loglevel");

  group.addCommand("Get the current log level and the highest level this build includes.", handleLogLevelCommand);

  auto& setCommand = group.addCommand("Set the log level until restart. Levels above the build's own are not available.", handleLogLevelCommand);
  setCommand.addArgument("level", "none, error, warn, info, debug or verbose", "warn");

  return group;
}
