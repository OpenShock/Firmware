#include "serial/command_handlers/common.h"

#include "CommandHandler.h"
#include "serialization/JsonSerial.h"

void _handleRFTransmitCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
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

OpenShock::Serial::CommandGroup OpenShock::Serial::CommandHandlers::RfTransmitHandler() {
  auto group = OpenShock::Serial::CommandGroup("rftransmit"sv);

  auto& cmd = group.addCommand("Transmit a RF command"sv, _handleRFTransmitCommand);
  cmd.addArgument(
    "json"sv,
    "must be a JSON object with the following fields"sv,
    "{\"model\":\"caixianlin\",\"id\":12345,\"type\":\"vibrate\",\"intensity\":99,\"durationMs\":500}",
    {"model      (string) Model of the shocker                    (\"caixianlin\", \"petrainer\", \"petrainer998dr\")"sv,
     "id         (number) ID of the shocker                       (0-65535)"sv,
     "type       (string) Type of the command                     (\"shock\", \"vibrate\", \"sound\", \"stop\")"sv,
     "intensity  (number) Intensity of the command                (0-255)"sv,
     "durationMs (number) Duration of the command in milliseconds (0-65535)"sv}
  );

  return group;
}

/*
  return OpenShock::Serial::CommandGroup {
    "rftransmit"sv,
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
*/
