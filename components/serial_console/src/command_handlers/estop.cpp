#include <freertos/FreeRTOS.h>

#include "serial_console/command_handlers/common.h"

#include "Convert.h"
#include "estop/EStopConfig.h"
#include "estop/EStopManager.h"

static void handleEStopEnabledCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_RESPONSE("EStopEnabled|%s", OpenShock::Config::GetEStopEnabled() ? "true" : "false");
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
    SERPR_RESPONSE("EStopPin|%hhi", static_cast<int8_t>(OpenShock::Config::GetEStopGpioPin()));
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

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::EStopHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("estop");

  group.addCommand("enabled", "Get the E-Stop enabled state.", handleEStopEnabledCommand);
  auto& setEnabledCommand = group.addCommand("enabled", "Set the E-Stop enabled state.", handleEStopEnabledCommand);
  setEnabledCommand.addArgument("enabled", "must be a boolean", "true");

  group.addCommand("pin", "Get the GPIO pin used for the E-Stop.", handleEStopPinCommand);
  auto& setPinCommand = group.addCommand("pin", "Set the GPIO pin used for the E-Stop.", handleEStopPinCommand);
  setPinCommand.addArgument("pin", "must be a number", "4");

  return group;
}
