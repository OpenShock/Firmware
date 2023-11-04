#pragma once

#include <cstdint>

namespace OpenShock::MessageHandlers::Server {
  void HandleBinary(const std::uint8_t* data, std::size_t len);
}
