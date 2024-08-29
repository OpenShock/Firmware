#pragma once

#include <cstdint>

namespace OpenShock {
  enum class WebSocketMessageType : uint8_t {
    Error,
    Disconnected,
    Connected,
    Text,
    Binary,
    Ping,
    Pong
  };
}
