#pragma once

#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <cstdint>

// TODO: This is bad architecture. Fix it.

namespace OpenShock::CommandHandler {
  void Init();
  bool SetRfTxPin(std::uint32_t txPin);
  bool HandleCommand(ShockerModelType shockerModel, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, unsigned int duration);
}  // namespace OpenShock::CommandHandler
