#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "Convert.h"
#include "EStopManager.h"

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

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::ESStopPinHandler()
{
  auto group = OpenShock::Serial::CommandGroup("estoppin"sv);

  auto& getCommand = group.addCommand("Get the GPIO pin used for the E-Stop."sv, _handleEStopPinCommand);

  auto& setCommand = group.addCommand("Set the GPIO pin used for the E-Stop."sv, _handleEStopPinCommand);
  setCommand.addArgument("pin"sv, "must be a number"sv, "4"sv);

  return group;
}
