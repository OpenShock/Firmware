#pragma once

#include "captiveportal/CaptiveDNSServer.h"
#include "Common.h"
#include "span.h"
#include "WebSocketDeFragger.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>

#include <freertos/task.h>

#include <cstdint>
#include <string_view>

namespace OpenShock::CaptivePortal {
  class CaptivePortalInstance {
    DISABLE_COPY(CaptivePortalInstance);
    DISABLE_MOVE(CaptivePortalInstance);

  public:
    CaptivePortalInstance(IPAddress apIP);
    ~CaptivePortalInstance();

    bool sendMessageTXT(uint8_t socketId, std::string_view data) { return m_socketServer.sendTXT(socketId, data.data(), data.length()); }
    bool sendMessageBIN(uint8_t socketId, tcb::span<const uint8_t> data) { return m_socketServer.sendBIN(socketId, data.data(), data.size()); }
    bool broadcastMessageTXT(std::string_view data) { return m_socketServer.broadcastTXT(data.data(), data.length()); }
    bool broadcastMessageBIN(tcb::span<const uint8_t> data) { return m_socketServer.broadcastBIN(data.data(), data.size()); }

  private:
    void task();
    void handleWebSocketClientConnected(uint8_t socketId);
    void handleWebSocketClientDisconnected(uint8_t socketId);
    void handleWebSocketEvent(uint8_t socketId, WebSocketMessageType type, tcb::span<const uint8_t> payload);

    AsyncWebServer m_webServer;
    WebSocketsServer m_socketServer;
    WebSocketDeFragger m_socketDeFragger;
    fs::LittleFSFS m_fileSystem;
    CaptiveDNSServer m_dnsServer;
    TaskHandle_t m_taskHandle;
  };
}  // namespace OpenShock::CaptivePortal
