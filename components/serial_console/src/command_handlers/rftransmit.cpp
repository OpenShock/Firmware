#include "serial_console/command_handlers/common.h"

#include "CommandHandler.h"
#include "serialization/JsonSerial.h"
#include "json/Json.h"

static void handleRFTransmitCommand(std::string_view arg, bool isAutomated)
{
  if (arg.empty()) {
    SERPR_ERROR("No command");
    return;
  }
  OpenShock::JSON::JsonDocument doc;
  if (!doc.parse(arg)) {
    SERPR_ERROR("Failed to parse JSON");
    return;
  }

  auto cmd = OpenShock::Serialization::JsonSerial::ParseShockerCommand(doc.root());

  if (!cmd) {
    SERPR_ERROR("Failed to parse shocker command");
    return;
  }

  if (!OpenShock::CommandHandler::HandleCommand(cmd->model, cmd->id, cmd->command, cmd->intensity, cmd->durationMs)) {
    SERPR_ERROR("Failed to send command");
    return;
  }

  SERPR_SUCCESS("Command sent");
}

OpenShock::SerialCmds::CommandGroup OpenShock::SerialCmds::CommandHandlers::RfTransmitHandler()
{
  auto group = OpenShock::SerialCmds::CommandGroup("rftransmit");

  static constexpr std::string_view kJsonFields[] = {
    "model      (string) Model of the shocker                    (\"caixianlin\", \"petrainer\", \"petrainer998dr\", \"wellturnt330\", \"d80\")",
    "id         (number) ID of the shocker                       (0-65535)",
    "type       (string) Type of the command                     (\"shock\", \"vibrate\", \"sound\", \"light\", \"stop\")",
    "intensity  (number) Intensity of the command                (0-255)",
    "durationMs (number) Duration of the command in milliseconds (0-65535)"
  };

  auto& cmd = group.addCommand("Transmit a RF command", handleRFTransmitCommand);
  cmd.addArgument("json", "must be a JSON object with the following fields:", "{\"model\":\"caixianlin\",\"id\":12345,\"type\":\"vibrate\",\"intensity\":99,\"durationMs\":500}", kJsonFields);

  return group;
}
