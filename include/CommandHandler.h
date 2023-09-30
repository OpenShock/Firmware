#pragma once

#include <cstdint>

// TODO: This is bad architecture. Fix it.

namespace ShockLink::CommandHandler {
  void Init();
  bool HandleCommand(std::uint16_t shockerId,
                     std::uint8_t method,
                     std::uint8_t intensity,
                     unsigned int duration,
                     std::uint8_t shockerModel);
}  // namespace ShockLink::CommandHandler
