#include "MessageHandlers/Server_Private.h"

#include "CommandHandler.h"
#include "ShockerModelType.h"

#include <esp_log.h>

#include <cstdint>

const char* const TAG = "ServerMessageHandlers";

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleShockerCommandList(std::uint8_t socketId, const OpenShock::Serialization::ServerToDeviceMessage* root) {
  auto msg = root->payload_as_ShockerCommandList();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as ShockerCommandList");
    return;
  }

  auto commands = msg->commands();
  if (commands == nullptr) {
    ESP_LOGE(TAG, "Received invalid command list from API");
    return;
  }

  ESP_LOGV(TAG, "Received command list from API (%d commands)", commands->size());

  for (auto command : *commands) {
    std::uint16_t id                   = command->id();
    std::uint8_t intensity             = command->intensity();
    std::uint32_t duration             = command->duration();
    OpenShock::ShockerModelType model  = command->model();
    OpenShock::ShockerCommandType type = command->type();

    ESP_LOGV(TAG, "   ID %u, Intensity %u, Duration %u, Model %s, Type %s", id, intensity, duration, OpenShock::Serialization::Types::EnumNameShockerModelType(model), OpenShock::Serialization::Types::EnumNameShockerCommandType(type));

    if (!OpenShock::CommandHandler::HandleCommand(model, id, type, intensity, duration)) {
      ESP_LOGE(TAG, "Remote command failed/rejected!");
    }
  }
}
