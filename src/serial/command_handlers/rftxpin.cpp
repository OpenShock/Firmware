#include "serial/command_handlers/common.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "Convert.h"
#include "SetRfPinResultCode.h"

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

  OpenShock::SetRfPinResultCode result = OpenShock::CommandHandler::SetRfTxPin(pin);

  switch (result) {
    case OpenShock::SetRfPinResultCode::InvalidPin:
      SERPR_ERROR("Invalid argument (invalid pin)");
      break;

    case OpenShock::SetRfPinResultCode::InternalError:
      SERPR_ERROR("Internal error while setting RF TX pin");
      break;

    case OpenShock::SetRfPinResultCode::Success:
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
