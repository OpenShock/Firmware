#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace OpenShock::MessageHandlers::WebSocket {
  // Largest message the handlers accept; transports should not buffer more than this.
  inline constexpr std::size_t MaxGatewayMessageSize = 4096;  // TODO: Profile this
  inline constexpr std::size_t MaxLocalMessageSize   = 4096;  // TODO: Profile this

  void HandleGatewayBinary(std::span<const uint8_t> data);
  void HandleLocalBinary(uint8_t socketId, std::span<const uint8_t> data);
}
