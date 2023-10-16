#pragma once

namespace OpenShock {
  enum class WebSocketMessageType {
    Error,
    Disconnected,
    Connected,
    Text,
    Binary,
    Ping,
    Pong
  };
}
