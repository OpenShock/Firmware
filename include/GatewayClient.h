#pragma once

#include "Common.h"
#include "GatewayClientState.h"

#include <WebSocketsClient.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <freertos/task.h>

namespace OpenShock {
  class GatewayClient {
    DISABLE_COPY(GatewayClient);
    DISABLE_MOVE(GatewayClient);

  public:
    GatewayClient(const std::string& authToken);
    ~GatewayClient();

    constexpr GatewayClientState state() const { return m_state; }

    void connect(const char* lcgFqdn);
    void disconnect();

    bool sendMessageTXT(std::string_view data);
    bool sendMessageBIN(const uint8_t* data, std::size_t length);

  private:
    void _loop();
    void _setState(GatewayClientState state);
    void _sendBootStatus();
    void _handleEvent(WStype_t type, uint8_t* payload, std::size_t length);

    WebSocketsClient m_webSocket;
    GatewayClientState m_state;
    TaskHandle_t m_loopTask;
  };
}  // namespace OpenShock
