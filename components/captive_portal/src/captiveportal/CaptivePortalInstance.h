#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dns_server/DNSServer.h"
#include "enums/WebSocketMessageType.h"
#include "fs/StaticFs.h"
#include "OpenShock.h"
#include "SimpleMutex.h"

#include <esp_http_server.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace OpenShock::CaptivePortal {
  // Shared so a detached job (account link) can keep the instance, and with it the server, alive until it answers.
  class CaptivePortalInstance : public std::enable_shared_from_this<CaptivePortalInstance> {
    DISABLE_COPY(CaptivePortalInstance);
    DISABLE_MOVE(CaptivePortalInstance);

  public:
    CaptivePortalInstance();
    ~CaptivePortalInstance();

    // WS sends invoked from arbitrary tasks (WiFiManager, GatewayConnectionManager,
    // the captive-portal manager task). httpd frames may only be written from the
    // httpd task, so these copy the payload into a bounded queue that the httpd task
    // drains. Return false when the message was dropped.
    bool sendMessageTXT(uint8_t socketId, std::string_view data);
    bool sendMessageBIN(uint8_t socketId, std::span<const uint8_t> data);
    bool broadcastMessageTXT(std::string_view data);
    bool broadcastMessageBIN(std::span<const uint8_t> data);
    bool hasClients();

    /// @brief Whether the HTTP and DNS servers came up; a failed instance must not be published.
    inline bool ok() const { return m_server != nullptr && m_dnsStarted; }

  private:
    static constexpr uint8_t MAX_WS_CLIENTS = 4;  // matches AP max_connection

    struct WsClient {
      bool used;
      int fd;
      bool reasmActive;                // a fragmented message is in progress
      WebSocketMessageType reasmType;  // opcode of an in-progress fragmented message
      std::vector<uint8_t> reasm;      // reassembly buffer for continuation frames
    };

    bool m_dnsStarted = false;

    // --- HTTP handlers that need instance state (recovered via req->user_ctx) ---
    static esp_err_t wsPostHandshake(httpd_req_t* req);
    static esp_err_t wsHandler(httpd_req_t* req);
    static esp_err_t staticFileHandler(httpd_req_t* req);

    bool startHttpServer();
    void registerHandlers();

    // --- WebSocket plumbing ---
    uint8_t onWsOpen(int fd);
    void onWsClose(int fd);
    void onWsFrame(int fd, httpd_ws_type_t opcode, bool final, std::span<const uint8_t> payload);
    void dispatchWsMessage(uint8_t socketId, WebSocketMessageType type, std::span<const uint8_t> payload);
    void handleWebSocketClientConnected(httpd_req_t* req, uint8_t socketId);
    void handleWebSocketClientDisconnected(uint8_t socketId);

    int fdForId(uint8_t socketId);
    int idForFd(int fd);  // -1 if not found

    bool queueSend(int fd, bool broadcast, bool binary, std::span<const uint8_t> data);
    static void wsDrainWork(void* arg);
    void drainSends();

    // Per-session free callback fired by httpd on socket close/LRU/timeout.
    static void wsSessCtxFree(void* ctx);

    httpd_handle_t m_server;
    StaticFs m_staticFs;
    const char* m_fsHash;
    DNSServer m_dnsServer;

    WsClient m_clients[MAX_WS_CLIENTS];
    SimpleMutex m_clientsMutex;

    struct WsSend {
      int fd;  // -1 to broadcast
      bool binary;
      std::vector<uint8_t> data;
    };

    // Sends waiting for the httpd task. httpd's own work mailbox is only a few slots deep (and shared with its
    // control messages), so it carries at most one pending drain request ("doorbell") instead of one per message.
    SimpleMutex m_sendMutex;
    std::deque<WsSend> m_sendQueue;
    std::atomic<bool> m_drainQueued = false;
  };
}  // namespace OpenShock::CaptivePortal
