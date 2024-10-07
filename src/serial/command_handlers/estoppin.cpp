#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "Convert.h"
#include "EStopManager.h"

void _handleEStopPinCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    gpio_num_t estopPin;
    if (!OpenShock::Config::GetEStopGpioPin(estopPin)) {
      SERPR_ERROR("Failed to get EStop pin from config");
      return;
    }

    // Get EStop pin
    SERPR_RESPONSE("EStopPin|%hhi", static_cast<int8_t>(estopPin));
    return;
  }

  uint8_t pin;
  if (!OpenShock::Convert::ToUint8(arg, pin)) {
    SERPR_ERROR("Invalid argument (number invalid or out of range)");
    return;
  }

  gpio_num_t estopPin = static_cast<gpio_num_t>(pin);

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

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::ESStopPinHandler() {
  auto group = OpenShock::Serial::CommandGroup("estoppin"sv);

  auto& getCommand = group.addCommand("Get the GPIO pin used for the E-Stop."sv, _handleEStopPinCommand);

  auto& setCommand = group.addCommand("Set the GPIO pin used for the E-Stop."sv, _handleEStopPinCommand);
  setCommand.addArgument("pin"sv, "must be a number"sv, "4"sv);

  return group;
}
