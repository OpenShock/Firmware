#pragma once

#include "enums/GatewayClientState.h"
#include "OpenShock.h"

#include <esp_event.h>
#include <esp_websocket_client.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

    void markPingReceived();

    bool loop();

  private:
    void _setState(GatewayClientState state);
    void _sendBootStatus();

    static void _eventHandler(void* arg, esp_event_base_t base, int32_t eventId, void* eventData);
    void _handleData(const esp_websocket_event_data_t* data);

    std::string m_headers;
    esp_websocket_client_handle_t m_client;
    GatewayClientState m_state;
    int64_t m_lastPingTimestamp;
    std::vector<uint8_t> m_binReasm;  // reassembly buffer for fragmented binary frames
  };
}  // namespace OpenShock
