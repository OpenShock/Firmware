#include "serial_console/command_handlers/common.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "Convert.h"
#include "enums/SetGPIOResultCode.h"

static void handleRfTxPinCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    auto configPin = OpenShock::Config::GetRFConfigTxPin();
    if (!configPin) {
      SERPR_ERROR("Failed to get RF TX pin from config");
      return;
    }

    // Get rmt pin
    SERPR_RESPONSE("RmtPin|%hhi", static_cast<int8_t>(*configPin));
    return;
  }

  auto txPin = OpenShock::Convert::ToGpioNum(arg);
  if (!txPin) {
    SERPR_ERROR("Invalid argument (number invalid or out of range)");
    return;
  }

  OpenShock::SetGPIOResultCode result = OpenShock::CommandHandler::SetRfTxPin(*txPin);

  switch (result) {
    case OpenShock::SetGPIOResultCode::InvalidPin:
      SERPR_ERROR("Invalid argument (invalid pin)");
      return;

    case OpenShock::SetGPIOResultCode::InternalError:
      SERPR_ERROR("Internal error while setting RF TX pin");
      return;

    case OpenShock::SetGPIOResultCode::PinInUse:
      SERPR_ERROR("Pin is in use by the E-Stop");
      return;

    case OpenShock::SetGPIOResultCode::Success:
      SERPR_SUCCESS("Saved config");
      return;
  }

  SERPR_ERROR("Unknown error while setting RF TX pin");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::RfTxPinHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("rftxpin"sv);

  group.addCommand("Get the GPIO pin used for the radio transmitter"sv, handleRfTxPinCommand);

  auto& setCommand = group.addCommand("Set the GPIO pin used for the radio transmitter"sv, handleRfTxPinCommand);
  setCommand.addArgument("pin"sv, "must be a number"sv, "15"sv);

  return group;
}
