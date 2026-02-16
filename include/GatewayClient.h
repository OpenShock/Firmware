#pragma once

#include "Common.h"
#include "GatewayClientState.h"

#include <WebSocketsClient.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace OpenShock {
  class GatewayClient {
    DISABLE_COPY(GatewayClient);
    DISABLE_MOVE(GatewayClient);

  public:
    GatewayClient(const std::string& authToken);
    ~GatewayClient();

    inline GatewayClientState state() const { return m_state; }

    void connect(const std::string& host, uint16_t port, const std::string& path);
    void disconnect();

    bool sendMessageTXT(std::string_view data);
    bool sendMessageBIN(std::span<const uint8_t> data);

    bool loop();

  private:
    void _setState(GatewayClientState state);
    void _sendBootStatus();
    void _handleEvent(WStype_t type, uint8_t* payload, std::size_t length);

    WebSocketsClient m_webSocket;
    GatewayClientState m_state;
  };
}  // namespace OpenShock
