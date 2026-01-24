#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "Convert.h"
#include "estop/EStopManager.h"

void _handleEStopEnabledCommand(std::string_view arg, bool isAutomated)
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
    SERPR_ERROR("Failed to set EStop enabled");
    return;
  }

  if (!OpenShock::Config::SetEStopEnabled(enabled)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

void _handleEStopPinCommand(std::string_view arg, bool isAutomated)
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
    SERPR_ERROR("Failed to set EStop pin");
    return;
  }

  if (!OpenShock::Config::SetEStopGpioPin(estopPin)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::EStopHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("estop"sv);

  auto& getEnabledCommand = group.addCommand("enabled"sv, "Get the E-Stop enabled state."sv, _handleEStopEnabledCommand);
  auto& setEnabledCommand = group.addCommand("enabled"sv, "Set the E-Stop enabled state."sv, _handleEStopEnabledCommand);
  setEnabledCommand.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  auto& getPinCommand = group.addCommand("pin"sv, "Get the GPIO pin used for the E-Stop."sv, _handleEStopPinCommand);
  auto& setPinCommand = group.addCommand("pin"sv, "Set the GPIO pin used for the E-Stop."sv, _handleEStopPinCommand);
  setPinCommand.addArgument("pin"sv, "must be a number"sv, "4"sv);

  return group;
}
