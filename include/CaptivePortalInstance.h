#pragma once

#include "WebSocketDeFragger.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>

#include <freertos/task.h>

#include <cstdint>
#include <string_view>

namespace OpenShock {
  class CaptivePortalInstance {
  public:
    CaptivePortalInstance();
    ~CaptivePortalInstance();

    bool sendMessageTXT(uint8_t socketId, std::string_view data) { return m_socketServer.sendTXT(socketId, data.data(), data.length()); }
    bool sendMessageBIN(uint8_t socketId, const uint8_t* data, std::size_t len) { return m_socketServer.sendBIN(socketId, data, len); }
    bool broadcastMessageTXT(std::string_view data) { return m_socketServer.broadcastTXT(data.data(), data.length()); }
    bool broadcastMessageBIN(const uint8_t* data, std::size_t len) { return m_socketServer.broadcastBIN(data, len); }

  private:
    static void task(void* arg);
    void handleWebSocketClientConnected(uint8_t socketId);
    void handleWebSocketClientDisconnected(uint8_t socketId);
    void handleWebSocketClientError(uint8_t socketId, uint16_t code, const char* message);
    void handleWebSocketEvent(uint8_t socketId, WebSocketMessageType type, const uint8_t* payload, std::size_t length);

    AsyncWebServer m_webServer;
    WebSocketsServer m_socketServer;
    WebSocketDeFragger m_socketDeFragger;
    fs::LittleFSFS m_fileSystem;
    DNSServer m_dnsServer;
    TaskHandle_t m_taskHandle;
  };
}  // namespace OpenShock
