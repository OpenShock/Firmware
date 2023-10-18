#pragma once

#include <WebSocketsClient.h>

#include <string>
#include <unordered_map>

namespace OpenShock {
  class GatewayClient {
  public:
    GatewayClient(const std::string& authToken, const std::string& fwVersionStr);
    ~GatewayClient();

    enum class State {
      Disconnected,
      Disconnecting,
      Connecting,
      Connected,
    };

    constexpr State state() const { return m_state; }

    void connect(const char* lcgFqdn);
    void disconnect();

    bool loop();

  private:
    void _sendKeepAlive();
    void _handleEvent(WStype_t type, std::uint8_t* payload, std::size_t length);

    WebSocketsClient m_webSocket;
    std::int64_t m_lastKeepAlive;
    State m_state;
  };
}  // namespace OpenShock
