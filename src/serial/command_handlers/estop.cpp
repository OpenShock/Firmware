#include "serial/command_handlers/common.h"

#include "config/Config.h"
#include "Convert.h"
#include "EStopManager.h"

static void handleGetEnabled(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Get command does not support parameters");
    return;
  }

  bool enabled;
  if (!OpenShock::Config::GetEStopEnabled(enabled)) {
    SERPR_ERROR("Failed to get EStop enabled from config");
    return;
  }

  // Get EStop enabled
  SERPR_RESPONSE("EStopEnabled|%s", enabled ? "true" : "false");
}

static void handleSetEnabled(std::string_view arg, bool isAutomated)
{
  bool enabled;
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

static void handleGetPin(std::string_view arg, bool isAutomated)
{
  if (!arg.empty()) {
    SERPR_ERROR("Get command does not support parameters");
    return;
  }

  gpio_num_t estopPin;
  if (!OpenShock::Config::GetEStopGpioPin(estopPin)) {
    SERPR_ERROR("Failed to get EStop pin from config");
    return;
  }

  // Get EStop pin
  SERPR_RESPONSE("EStopPin|%hhi", static_cast<int8_t>(estopPin));
}

static void handleSetPin(std::string_view arg, bool isAutomated)
{
  gpio_num_t estopPin;
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

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::EStopHandler()
{
  auto group = OpenShock::Serial::CommandGroup("estop"sv);

  auto& getEnabledCommand = group.addCommand("enabled"sv, "Get the E-Stop enabled state."sv, handleGetEnabled);
  auto& setEnabledCommand = group.addCommand("enabled"sv, "Set the E-Stop enabled state."sv, handleSetEnabled);
  setEnabledCommand.addArgument("enabled"sv, "must be a boolean"sv, "true"sv);

  auto& getPinCommand = group.addCommand("pin"sv, "Get the GPIO pin used for the E-Stop."sv, handleGetPin);
  auto& setPinCommand = group.addCommand("pin"sv, "Set the GPIO pin used for the E-Stop."sv, handleSetPin);
  setPinCommand.addArgument("pin"sv, "must be a number"sv, "4"sv);

  return group;
}
