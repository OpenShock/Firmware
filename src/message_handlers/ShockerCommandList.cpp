#include "message_handlers/ShockerCommandList.h"

const char* const TAG = "ShockerCommandHandler";

#include "CommandHandler.h"
#include "Logging.h"
#include "ShockerModelType.h"

#include <cstdint>

using FbsModelType   = OpenShock::Serialization::Types::ShockerModelType;
using FbsCommandType = OpenShock::Serialization::Types::ShockerCommandType;

void OpenShock::MessageHandlers::HandleShockerCommandList(const OpenShock::Serialization::Common::ShockerCommandList* cmdList)
{
  auto commands = cmdList->commands();
  if (commands == nullptr) {
    OS_LOGE(TAG, "Received invalid command list");
    return;
  }

  OS_LOGV(TAG, "Received command list (%u commands)", commands->size());

  for (auto command : *commands) {
    uint16_t id                   = command->id();
    uint8_t intensity             = command->intensity();
    uint16_t durationMs           = command->duration();
    FbsModelType fbsModel         = command->model();
    FbsCommandType fbsCommandType = command->type();

    ShockerModelType model;
    switch (fbsModel) {
      case FbsModelType::CaiXianlin:
        model = ShockerModelType::CaiXianlin;
        break;
      case FbsModelType::Petrainer:
        model = ShockerModelType::Petrainer;
        break;
      case FbsModelType::Petrainer998DR:
        model = ShockerModelType::Petrainer998DR;
        break;
      case FbsModelType::WellturnT330:
        model = ShockerModelType::WellturnT330;
        break;
      default:
        OS_LOGE(TAG, "Unsupported shocker model: %s", OpenShock::Serialization::Types::EnumNameShockerModelType(fbsModel));
        continue;
    }

    ShockerCommandType commandType;
    switch (fbsCommandType) {
      case FbsCommandType::Stop:
        commandType = ShockerCommandType::Stop;
        break;
      case FbsCommandType::Shock:
        commandType = ShockerCommandType::Shock;
        break;
      case FbsCommandType::Vibrate:
        commandType = ShockerCommandType::Vibrate;
        break;
      case FbsCommandType::Sound:
        commandType = ShockerCommandType::Sound;
        break;
      default:
        OS_LOGE(TAG, "Unsupported command type: %s", OpenShock::Serialization::Types::EnumNameShockerCommandType(fbsCommandType));
        continue;
    }

    if (!CommandHandler::HandleCommand(model, id, commandType, intensity, durationMs)) {
      OS_LOGE(TAG, "Command failed/rejected!");
    }
  }
}
