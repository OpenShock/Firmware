#include "serial/command_handlers/common.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "Convert.h"
#include "SetGPIOResultCode.h"

void _handleRfTxPinCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    uint8_t txPin;
    if (!OpenShock::Config::GetRFConfigTxPin(txPin)) {
      SERPR_ERROR("Failed to get RF TX pin from config");
      return;
    }

    // Get rmt pin
    SERPR_RESPONSE("RmtPin|%u", txPin);
    return;
  }

  uint8_t pin;
  if (!OpenShock::Convert::ToUint8(arg, pin)) {
    SERPR_ERROR("Invalid argument (number invalid or out of range)");
  }

  OpenShock::SetGPIOResultCode result = OpenShock::CommandHandler::SetRfTxPin(static_cast<uint8_t>(pin));

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

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::RfTxPinHandler() {
  auto group = OpenShock::Serial::CommandGroup("rftxpin"sv);

  auto getter = group.addCommand("Get the GPIO pin used for the radio transmitter"sv, _handleRfTxPinCommand);

  auto setter = group.addCommand("Set the GPIO pin used for the radio transmitter"sv, _handleRfTxPinCommand);
  setter.addArgument("pin"sv, "must be a number"sv, "15"sv);

  return group;
}
