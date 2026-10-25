#include "message_handlers/ShockerCommandList.h"

const char* const TAG = "ShockerCommandHandler";

#include "CommandHandler.h"
#include "enums/ShockerModelType.h"
#include "Logging.h"

#include <cstdint>
#include <optional>

using FbsModelType   = OpenShock::Serialization::Types::ShockerModelType;
using FbsCommandType = OpenShock::Serialization::Types::ShockerCommandType;

// No default in these switches: -Wswitch flags new schema enumerators. The values come from the network and may be
// out of range, so they fall through to std::nullopt.
static std::optional<OpenShock::ShockerModelType> toShockerModelType(FbsModelType fbsModel)
{
  switch (fbsModel) {
    case FbsModelType::CaiXianlin:
      return OpenShock::ShockerModelType::CaiXianlin;
    case FbsModelType::Petrainer:
      return OpenShock::ShockerModelType::Petrainer;
    case FbsModelType::Petrainer998DR:
      return OpenShock::ShockerModelType::Petrainer998DR;
    case FbsModelType::WellturnT330:
      return OpenShock::ShockerModelType::WellturnT330;
  }

  return std::nullopt;
}

static std::optional<OpenShock::ShockerCommandType> toShockerCommandType(FbsCommandType fbsCommandType)
{
  switch (fbsCommandType) {
    case FbsCommandType::Stop:
      return OpenShock::ShockerCommandType::Stop;
    case FbsCommandType::Shock:
      return OpenShock::ShockerCommandType::Shock;
    case FbsCommandType::Vibrate:
      return OpenShock::ShockerCommandType::Vibrate;
    case FbsCommandType::Sound:
      return OpenShock::ShockerCommandType::Sound;
  }

  return std::nullopt;
}

void OpenShock::MessageHandlers::HandleShockerCommandList(const OpenShock::Serialization::Common::ShockerCommandList* cmdList)
{
  auto commands = cmdList->commands();
  if (commands == nullptr) {
    OS_LOGE(TAG, "Received invalid command list");
    return;
  }

  OS_LOGV(TAG, "Received command list (%u commands)", static_cast<unsigned>(commands->size()));

  for (auto command : *commands) {
    uint16_t id                   = command->id();
    uint8_t intensity             = command->intensity();
    uint16_t durationMs           = command->duration();
    FbsModelType fbsModel         = command->model();
    FbsCommandType fbsCommandType = command->type();

    auto model = toShockerModelType(fbsModel);
    if (!model) {
      OS_LOGE(TAG, "Unsupported shocker model: %s", OpenShock::Serialization::Types::EnumNameShockerModelType(fbsModel));
      continue;
    }

    auto commandType = toShockerCommandType(fbsCommandType);
    if (!commandType) {
      OS_LOGE(TAG, "Unsupported command type: %s", OpenShock::Serialization::Types::EnumNameShockerCommandType(fbsCommandType));
      continue;
    }

    if (!CommandHandler::HandleCommand(*model, id, *commandType, intensity, durationMs)) {
      OS_LOGE(TAG, "Command failed/rejected!");
    }
  }
}
