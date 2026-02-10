#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "CommandHandler.h"
#include "Logging.h"
#include "ShockerModelType.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Server;
using FbsModelType   = OpenShock::Serialization::Types::ShockerModelType;
using FbsCommandType = OpenShock::Serialization::Types::ShockerCommandType;

void _Private::HandleShockerCommandList(const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
{
  auto msg = root->payload_as_ShockerCommandList();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as ShockerCommandList");
    return;
  }

  auto commands = msg->commands();
  if (commands == nullptr) {
    OS_LOGE(TAG, "Received invalid command list from API");
    return;
  }

  OS_LOGV(TAG, "Received command list from API (%u commands)", commands->size());

  for (auto command : *commands) {
    uint16_t id                   = command->id();
    uint8_t intensity             = command->intensity();
    uint16_t durationMs           = command->duration();
    FbsModelType fbsModel         = command->model();
    FbsCommandType fbsCommandType = command->type();

    OpenShock::ShockerModelType model;
    switch (fbsModel) {
      case FbsModelType::CaiXianlin:
        model = OpenShock::ShockerModelType::CaiXianlin;
        break;
      case FbsModelType::Petrainer:
        model = OpenShock::ShockerModelType::Petrainer;
        break;
      case FbsModelType::Petrainer998DR:
        model = OpenShock::ShockerModelType::Petrainer998DR;
        break;
      // case FbsModelType::T330:
      //   model = OpenShock::ShockerModelType::T330;
      //   break;
      // case FbsModelType::D80:
      //   model = OpenShock::ShockerModelType::D80;
      //   break;
      default:
        OS_LOGE(TAG, "Unsupported shocker model: %s", OpenShock::Serialization::Types::EnumNameShockerModelType(fbsModel));
        continue;
    }

    OpenShock::ShockerCommandType commandType;
    switch (fbsCommandType) {
      case FbsCommandType::Stop:
        commandType = OpenShock::ShockerCommandType::Stop;
        break;
      case FbsCommandType::Shock:
        commandType = OpenShock::ShockerCommandType::Shock;
        break;
      case FbsCommandType::Vibrate:
        commandType = OpenShock::ShockerCommandType::Vibrate;
        break;
      case FbsCommandType::Sound:
        commandType = OpenShock::ShockerCommandType::Sound;
        break;
      // case FbsCommandType::Light:
      //   commandType = OpenShock::ShockerCommandType::Light;
      //   break;
      default:
        OS_LOGE(TAG, "Unsupported command type: %s", OpenShock::Serialization::Types::EnumNameShockerCommandType(fbsCommandType));
        continue;
    }

    if (!OpenShock::CommandHandler::HandleCommand(model, id, commandType, intensity, durationMs)) {
      OS_LOGE(TAG, "Remote command failed/rejected!");
    }
  }
}
