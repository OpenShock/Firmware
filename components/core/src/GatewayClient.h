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

    // Takes the client out of service ahead of destruction: reports it disconnected once, then stops dispatching
    // received messages, posting state changes, sending the boot status and (re)connecting. Don't call it under a lock
    // other tasks wait on: it posts an event.
    void retire();

    bool loop();

  private:
    void _setState(GatewayClientState state);
    void _sendBootStatus();
    bool _send(ws_transport_opcodes_t opcode, const uint8_t* data, std::size_t len);

    static void _eventHandler(void* arg, esp_event_base_t base, int32_t eventId, void* eventData);
    void _handleData(const esp_websocket_event_data_t* data);

    esp_websocket_client_handle_t _takeHandle();
    void _destroyHandle();

    std::string m_headers;
    SimpleMutex m_handleMutex;                            // Held across sends from other tasks so the handle can't be destroyed mid-send
    std::atomic<esp_websocket_client_handle_t> m_client;  // Written under m_handleMutex
    std::atomic<TaskHandle_t> m_wsTask;                   // The websocket task, recorded from its event callbacks
    std::atomic<GatewayClientState> m_state;              // Written from both the owning task and the websocket task
    std::atomic<int64_t> m_lastPingTimestamp;
    std::atomic<bool> m_retired;

    // Reassembly of fragmented/chunked binary messages, only touched from the websocket task
    std::vector<uint8_t> m_binReasm;
    bool m_binReasmActive;
    bool m_binReasmDiscard;
  };
}  // namespace OpenShock
