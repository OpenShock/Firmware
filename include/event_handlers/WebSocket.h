#pragma once

#include <cstdint>

namespace OpenShock::EventHandlers::WebSocket {
  void HandleGatewayBinary(const std::uint8_t* data, std::size_t len);
  void HandleLocalBinary(std::uint8_t socketId, const std::uint8_t* data, std::size_t len);
}
