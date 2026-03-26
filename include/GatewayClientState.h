#pragma once

#include <cstdint>

namespace OpenShock {
  enum class GatewayClientState : uint8_t {
    Disconnected,
    Disconnecting,
    Connecting,
    Connected,
  };
}  // namespace OpenShock
