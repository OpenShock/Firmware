#pragma once

#include <cstdint>

namespace OpenShock::MessageHandlers::Local {
  void HandleBinary(std::uint8_t socketId, const std::uint8_t* data, std::size_t len);
}
