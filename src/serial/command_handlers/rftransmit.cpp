#include "serial/command_handlers/index.h"

#include "serial/command_handlers/impl/common.h"
#include "serial/command_handlers/impl/SerialCmdHandler.h"

#include "CommandHandler.h"
#include "serialization/JsonSerial.h"

void _handleRFTransmitCommand(OpenShock::StringView arg) {
  if (arg.isNullOrEmpty()) {
    SERPR_ERROR("No command");
    return;
  }
  cJSON* root = cJSON_ParseWithLength(arg.data(), arg.length());
  if (root == nullptr) {
    SERPR_ERROR("Failed to parse JSON: %s", cJSON_GetErrorPtr());
    return;
  }

  OpenShock::Serialization::JsonSerial::ShockerCommand cmd;
  bool parsed = OpenShock::Serialization::JsonSerial::ParseShockerCommand(root, cmd);

  cJSON_Delete(root);

  if (!parsed) {
    SERPR_ERROR("Failed to parse shocker command");
    return;
  }

  if (!OpenShock::CommandHandler::HandleCommand(cmd.model, cmd.id, cmd.command, cmd.intensity, cmd.durationMs)) {
    SERPR_ERROR("Failed to send command");
    return;
  }

  SERPR_SUCCESS("Command sent");
}

OpenShock::Serial::CommandHandlerEntry OpenShock::Serial::CommandHandlers::RfTransmitHandler() {
  return OpenShock::Serial::CommandHandlerEntry {
    "rftransmit"_sv,
    R"(rftransmit <json>
  Transmit a RF command
  Arguments:
    <json> must be a JSON object with the following fields:
      model      (string) Model of the shocker                    ("caixianlin", "petrainer", "petrainer998dr")
      id         (number) ID of the shocker                       (0-65535)
      type       (string) Type of the command                     ("shock", "vibrate", "sound", "stop")
      intensity  (number) Intensity of the command                (0-255)
      durationMs (number) Duration of the command in milliseconds (0-65535)
  Example:
    rftransmit {"model":"caixianlin","id":12345,"type":"vibrate","intensity":99,"durationMs":500}
)",
    _handleRFTransmitCommand,
  };
}
