#pragma once

#include "StringView.h"

#include <WebSocketsClient.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace OpenShock {
  class GatewayClient {
  public:
    GatewayClient(const std::string& authToken);
    ~GatewayClient();

    enum class State : std::uint8_t {
      Disconnected,
      Disconnecting,
      Connecting,
      Connected,
    };

    constexpr State state() const { return m_state; }

    void connect(const char* lcgFqdn);
    void disconnect();

    bool sendMessageTXT(StringView data);
    bool sendMessageBIN(const std::uint8_t* data, std::size_t length);

    bool loop();

  private:
    void _sendKeepAlive();
    void _sendBootStatus();
    void _handleEvent(WStype_t type, std::uint8_t* payload, std::size_t length);

    WebSocketsClient m_webSocket;
    std::int64_t m_lastKeepAlive;
    State m_state;
  };
}  // namespace OpenShock
