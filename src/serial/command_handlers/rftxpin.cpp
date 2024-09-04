#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/CommandEntry.h"
#include "serial/command_handlers/impl/common.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "SetRfPinResultCode.h"

void _handleRfTxPinCommand(OpenShock::StringView arg) {
  if (arg.isNullOrEmpty()) {
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
  if (!OpenShock::IntConv::stou8(arg, pin)) {
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

OpenShock::Serial::CommandHandlers::CommandEntry OpenShock::Serial::CommandHandlers::RfTxPinHandler() {
  auto getter = OpenShock::Serial::CommandHandlers::CommandEntry("rftxpin"_sv, "Get the GPIO pin used for the radio transmitter"_sv, _handleRfTxPinCommand);
  auto setter = OpenShock::Serial::CommandHandlers::CommandEntry("rftxpin"_sv, "Set the GPIO pin used for the radio transmitter"_sv, _handleRfTxPinCommand);
  setter.addArgument("pin"_sv, "must be a number"_sv, "15"_sv);

}
