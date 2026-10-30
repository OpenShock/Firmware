#include "serial_console/command_handlers/common.h"

#include "config/Config.h"
#include "Convert.h"
#include "estop/EStopManager.h"

static void handleEStopEnabledCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    auto configEnabled = OpenShock::Config::GetEStopEnabled();
    if (!configEnabled) {
      SERPR_ERROR("Failed to get EStop enabled from config");
      return;
    }

    // Get EStop enabled
    SERPR_RESPONSE("EStopEnabled|%s", *configEnabled ? "true" : "false");
    return;
  }

  auto enabled = OpenShock::Convert::ToBool(arg);
  if (!enabled) {
    SERPR_ERROR("Invalid argument (must be a boolean)");
    return;
  }

  if (!OpenShock::EStopManager::SetEStopEnabled(*enabled)) {
    SERPR_ERROR("Failed to set EStop enabled (refused while the EStop is active)");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

static void handleEStopPinCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    auto configPin = OpenShock::Config::GetEStopGpioPin();
    if (!configPin) {
      SERPR_ERROR("Failed to get EStop pin from config");
      return;
    }

    // Get EStop pin
    SERPR_RESPONSE("EStopPin|%hhi", static_cast<int8_t>(*configPin));
    return;
  }

  auto estopPin = OpenShock::Convert::ToGpioNum(arg);
  if (!estopPin) {
    SERPR_ERROR("Invalid argument (number invalid or out of range)");
    return;
  }

  if (!OpenShock::EStopManager::SetEStopPin(*estopPin)) {
    SERPR_ERROR("Failed to set EStop pin (invalid pin, or refused while the EStop is active)");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

static void handleEStopLatchingCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    auto configLatching = OpenShock::Config::GetEStopLatching();
    if (!configLatching) {
      SERPR_ERROR("Failed to get EStop latching from config");
      return;
    }

    // Get EStop latching
    SERPR_RESPONSE("EStopLatching|%s", *configLatching ? "true" : "false");
    return;
  }

  auto latching = OpenShock::Convert::ToBool(arg);
  if (!latching) {
    SERPR_ERROR("Invalid argument (must be a boolean)");
    return;
  }

  if (!OpenShock::EStopManager::SetEStopLatching(*latching)) {
    SERPR_ERROR("Failed to set EStop latching (refused while the EStop is active)");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::EStopHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("estop"sv);

  group.addCommand("enabled"sv, "Get the E-Stop enabled state."sv, handleEStopEnabledCommand);
  auto& setEnabledCommand = group.addCommand("enabled"sv, "Set the E-Stop enabled state."sv, handleEStopEnabledCommand);
  setEnabledCommand.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  group.addCommand("pin"sv, "Get the GPIO pin used for the E-Stop."sv, handleEStopPinCommand);
  auto& setPinCommand = group.addCommand("pin"sv, "Set the GPIO pin used for the E-Stop."sv, handleEStopPinCommand);
  setPinCommand.addArgument("pin"sv, "must be a number"sv, "4"sv);

  group.addCommand("latching"sv, "Get whether the E-Stop is a latching switch."sv, handleEStopLatchingCommand);
  auto& setLatchingCommand = group.addCommand("latching"sv, "Set whether the E-Stop is a latching switch (clears when released) or a momentary button (hold to clear)."sv, handleEStopLatchingCommand);
  setLatchingCommand.addArgument("latching"sv, "must be a boolean"sv, "true"sv);

  return group;
}
