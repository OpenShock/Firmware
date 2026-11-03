#pragma once

#include <cstdint>
#include <span>

namespace OpenShock {
  class GatewayClient;
}

namespace OpenShock::MessageHandlers::WebSocket {
  /// @brief Handles a message received on `client`, which replies go back through.
  void HandleGatewayBinary(GatewayClient& client, std::span<const uint8_t> data);
  void HandleLocalBinary(uint8_t socketId, std::span<const uint8_t> data);
}  // namespace OpenShock::MessageHandlers::WebSocket
