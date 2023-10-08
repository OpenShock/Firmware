#pragma once

#include "ShockerCommandType.h"

#include <cstdint>

// TODO: This is bad architecture. Fix it.

namespace OpenShock::CommandHandler {
  void Init();
  bool HandleCommand(std::uint16_t shockerId, OpenShock::ShockerCommandType type, std::uint8_t intensity, unsigned int duration, std::uint8_t shockerModel);
}  // namespace OpenShock::CommandHandler
