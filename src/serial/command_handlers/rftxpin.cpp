#include "serial/command_handlers/common.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "Convert.h"
#include "SetGPIOResultCode.h"

static void handleGet(std::string_view arg, bool isAutomated)
{
  gpio_num_t txPin;
  if (!OpenShock::Config::GetRFConfigTxPin(txPin)) {
    SERPR_ERROR("Failed to get RF TX pin from config");
    return;
  }

  // Get rmt pin
  SERPR_RESPONSE("RmtPin|%hhi", static_cast<int8_t>(txPin));
}

static void handleSet(std::string_view arg, bool isAutomated)
{
  gpio_num_t txPin;
  if (!OpenShock::Convert::ToGpioNum(arg, txPin)) {
    SERPR_ERROR("Invalid argument (number invalid or out of range)");
  }

  OpenShock::SetGPIOResultCode result = OpenShock::CommandHandler::SetRfTxPin(txPin);

  switch (result) {
    case OpenShock::SetGPIOResultCode::InvalidPin:
      SERPR_ERROR("Invalid argument (invalid pin)");
      break;

    case OpenShock::SetGPIOResultCode::InternalError:
      SERPR_ERROR("Internal error while setting RF TX pin");
      break;

    case OpenShock::SetGPIOResultCode::Success:
      SERPR_SUCCESS("Saved config");
      break;

    default:
      SERPR_ERROR("Unknown error while setting RF TX pin");
      break;
  }
}

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::RfTxPinHandler()
{
  auto group = OpenShock::Serial::CommandGroup("rftxpin"sv);

  auto& getCommand = group.addCommand("Get the GPIO pin used for the radio transmitter"sv, handleGet);

  auto& setCommand = group.addCommand("set"sv, "Set the GPIO pin used for the radio transmitter"sv, handleSet);
  setCommand.addArgument("pin"sv, "must be a number"sv, "15"sv);

  return group;
}
