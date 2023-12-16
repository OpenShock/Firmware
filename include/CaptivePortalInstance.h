#pragma once

#include "WebSocketDeFragger.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

namespace OpenShock {
  class CaptivePortalInstance {
  public:
    CaptivePortalInstance();
    ~CaptivePortalInstance();

    bool sendMessageTXT(std::uint8_t socketId, const char* data, std::size_t len) { return m_socketServer.sendTXT(socketId, data, len); }
    bool sendMessageBIN(std::uint8_t socketId, const std::uint8_t* data, std::size_t len) { return m_socketServer.sendBIN(socketId, data, len); }
    bool broadcastMessageTXT(const char* data, std::size_t len) { return m_socketServer.broadcastTXT(data, len); }
    bool broadcastMessageBIN(const std::uint8_t* data, std::size_t len) { return m_socketServer.broadcastBIN(data, len); }

  private:
    static void task(void* arg);
    void handleWebSocketClientConnected(std::uint8_t socketId);
    void handleWebSocketClientDisconnected(std::uint8_t socketId);
    void handleWebSocketClientError(std::uint8_t socketId, std::uint16_t code, const char* message);
    void handleWebSocketEvent(std::uint8_t socketId, WebSocketMessageType type, const std::uint8_t* payload, std::size_t length);

    AsyncWebServer m_webServer;
    WebSocketsServer m_socketServer;
    WebSocketDeFragger m_socketDeFragger;
    DNSServer m_dnsServer;
    TaskHandle_t m_taskHandle;
  };
}  // namespace OpenShock
