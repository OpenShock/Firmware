#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace OpenShock {
  class GatewayClient;
}

namespace OpenShock::MessageHandlers::WebSocket {
  // Largest message the handlers accept; transports should not buffer more than this.
  inline constexpr std::size_t MaxGatewayMessageSize = 4096;  // TODO: Profile this
  inline constexpr std::size_t MaxLocalMessageSize   = 4096;  // TODO: Profile this

  /// @brief Handles a message received on `client`, which replies go back through.
  void HandleGatewayBinary(GatewayClient& client, std::span<const uint8_t> data);
  void HandleLocalBinary(uint8_t socketId, std::span<const uint8_t> data);
}  // namespace OpenShock::MessageHandlers::WebSocket
