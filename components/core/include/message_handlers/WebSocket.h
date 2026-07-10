#pragma once

#include <cstdint>
#include <span>

namespace OpenShock::MessageHandlers::WebSocket {
  void HandleGatewayBinary(std::span<const uint8_t> data);
  void HandleLocalBinary(uint8_t socketId, std::span<const uint8_t> data);
}
