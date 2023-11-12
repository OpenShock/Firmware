#pragma once

#include <cstdint>

namespace OpenShock {
  enum class WebSocketMessageType : std::uint8_t {
    Error,
    Disconnected,
    Connected,
    Text,
    Binary,
    Ping,
    Pong
  };
}
