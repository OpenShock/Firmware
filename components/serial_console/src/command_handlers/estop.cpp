#include "serial_console/command_handlers/common.h"

#include "config/Config.h"
#include "Convert.h"
#include "estop/EStopManager.h"

static void handleEStopEnabledCommand(std::string_view arg, bool isAutomated)
{
  bool enabled;
  if (arg.empty()) {
    if (!OpenShock::Config::GetEStopEnabled(enabled)) {
      SERPR_ERROR("Failed to get EStop enabled from config");
      return;
    }

    // Get EStop enabled
    SERPR_RESPONSE("EStopEnabled|%s", enabled ? "true" : "false");
    return;
  }

  if (!OpenShock::Convert::ToBool(arg, enabled)) {
    SERPR_ERROR("Invalid argument (must be a boolean)");
    return;
  }

  if (!OpenShock::EStopManager::SetEStopEnabled(enabled)) {
    SERPR_ERROR("Failed to set EStop enabled (refused while the EStop is active)");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

static void handleEStopPinCommand(std::string_view arg, bool isAutomated)
{
  gpio_num_t estopPin;
  if (arg.empty()) {
    if (!OpenShock::Config::GetEStopGpioPin(estopPin)) {
      SERPR_ERROR("Failed to get EStop pin from config");
      return;
    }

    // Get EStop pin
    SERPR_RESPONSE("EStopPin|%hhi", static_cast<int8_t>(estopPin));
    return;
  }

  if (!OpenShock::Convert::ToGpioNum(arg, estopPin)) {
    SERPR_ERROR("Invalid argument (number invalid or out of range)");
    return;
  }

  if (!OpenShock::EStopManager::SetEStopPin(estopPin)) {
    SERPR_ERROR("Failed to set EStop pin (invalid pin, or refused while the EStop is active)");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

static void handleEStopLatchingCommand(std::string_view arg, bool isAutomated)
{
  bool latching;
  if (arg.empty()) {
    if (!OpenShock::Config::GetEStopLatching(latching)) {
      SERPR_ERROR("Failed to get EStop latching from config");
      return;
    }

    // Get EStop latching
    SERPR_RESPONSE("EStopLatching|%s", latching ? "true" : "false");
    return;
  }

  if (!OpenShock::Convert::ToBool(arg, latching)) {
    SERPR_ERROR("Invalid argument (must be a boolean)");
    return;
  }

  if (!OpenShock::EStopManager::SetEStopLatching(latching)) {
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
