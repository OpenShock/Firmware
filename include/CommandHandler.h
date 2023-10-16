#pragma once

#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <cstdint>

// TODO: This is bad architecture. Fix it.

namespace OpenShock::CommandHandler {
  bool Init();
  bool Ok();
  bool SetRfTxPin(std::uint8_t txPin);
  bool HandleCommand(ShockerModelType shockerModel, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, std::uint16_t durationMs);
}  // namespace OpenShock::CommandHandler
