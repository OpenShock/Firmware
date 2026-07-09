#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dns_server/DNSServer.h"
#include "fs/StaticFs.h"
#include "enums/WebSocketMessageType.h"
#include "OpenShock.h"
#include "SimpleMutex.h"

#include <esp_http_server.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace OpenShock::CaptivePortal {
  class CaptivePortalInstance {
    DISABLE_COPY(CaptivePortalInstance);
    DISABLE_MOVE(CaptivePortalInstance);

  public:
    CaptivePortalInstance();
    ~CaptivePortalInstance();

    // WS sends invoked from arbitrary tasks (WiFiManager, GatewayConnectionManager,
    // the captive-portal manager task). httpd frames may only be written from the
    // httpd task, so these copy the payload and marshal it via httpd_queue_work.
    bool sendMessageTXT(uint8_t socketId, std::string_view data);
    bool sendMessageBIN(uint8_t socketId, std::span<const uint8_t> data);
    bool broadcastMessageTXT(std::string_view data);
    bool broadcastMessageBIN(std::span<const uint8_t> data);
    bool hasClients();

  private:
    static constexpr uint8_t MAX_WS_CLIENTS = 4;  // matches AP max_connection

    struct WsClient {
      bool used;
      int fd;
      WebSocketMessageType reasmType;  // opcode of an in-progress fragmented message
      std::vector<uint8_t> reasm;      // reassembly buffer for continuation frames
    };

    // --- HTTP handlers that need instance state (recovered via req->user_ctx) ---
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

    // Per-session free callback fired by httpd on socket close/LRU/timeout.
    static void wsSessCtxFree(void* ctx);

    httpd_handle_t m_server;
    StaticFs m_staticFs;
    const char* m_fsHash;
    DNSServer m_dnsServer;

    WsClient m_clients[MAX_WS_CLIENTS];
    SimpleMutex m_clientsMutex;
  };
}  // namespace OpenShock::CaptivePortal
