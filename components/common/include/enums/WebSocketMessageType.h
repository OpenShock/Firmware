#pragma once

#include <cstdint>

namespace OpenShock {
  enum class WebSocketMessageType : uint8_t {
    Disconnected,
    Connected,
    Text,
    Binary,
    Ping,
    Pong
  };
}  // namespace OpenShock
