#pragma once

#include "enums/GatewayClientState.h"
#include "OpenShock.h"
#include "SimpleMutex.h"

#include <esp_event.h>
#include <esp_websocket_client.h>

#include <atomic>
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

    bool sendMessageTXT(std::string_view data);
    bool sendMessageBIN(std::span<const uint8_t> data);

    void markPingReceived();

    // Stops dispatching received messages; used when the client is taken out of service ahead of destruction.
    inline void retire() { m_retired.store(true); }

    bool loop();

  private:
    void _setState(GatewayClientState state);
    void _sendBootStatus();

    static void _eventHandler(void* arg, esp_event_base_t base, int32_t eventId, void* eventData);
    void _handleData(const esp_websocket_event_data_t* data);

    esp_websocket_client_handle_t _takeHandle();
    void _destroyHandle();

    std::string m_headers;
    // The handle is created and destroyed by the owning (main) task but used by every task that sends through
    // GatewayConnectionManager (OTA progress, the websocket task's own pong). m_state alone cannot protect it: a sender
    // that passed the state check may still be inside esp_websocket_client_send_*() when the owner tears the handle
    // down, and destroy frees the handle's locks without taking them. The mutex is held for the whole send and for
    // taking the handle out, never across the (blocking) destroy itself; see _destroyHandle().
    SimpleMutex m_handleMutex;
    esp_websocket_client_handle_t m_client;   // Guarded by m_handleMutex
    std::atomic<GatewayClientState> m_state;  // Written from both the owning task and the websocket task
    std::atomic<int64_t> m_lastPingTimestamp;
    std::atomic<bool> m_retired;

    // Reassembly of fragmented/chunked binary messages, only touched from the websocket task
    std::vector<uint8_t> m_binReasm;
    bool m_binReasmActive;
    bool m_binReasmDiscard;
  };
}  // namespace OpenShock
