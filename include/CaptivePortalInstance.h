#pragma once

#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>

#include <cstdint>

namespace OpenShock {
  class CaptivePortalInstance {
  public:
    CaptivePortalInstance();
    ~CaptivePortalInstance();

    bool sendMessageTXT(std::uint8_t socketId, const char* data, std::size_t len) { return socketServer.sendTXT(socketId, data, len); }
    bool sendMessageBIN(std::uint8_t socketId, const std::uint8_t* data, std::size_t len) { return socketServer.sendBIN(socketId, data, len); }
    bool broadcastMessageTXT(const char* data, std::size_t len) { return socketServer.broadcastTXT(data, len); }
    bool broadcastMessageBIN(const std::uint8_t* data, std::size_t len) { return socketServer.broadcastBIN(data, len); }

    void loop() { socketServer.loop(); }

  private:
    void handleWebSocketClientConnected(std::uint8_t socketId);
    void handleWebSocketClientDisconnected(std::uint8_t socketId);
    void handleWebSocketClientError(std::uint8_t socketId, std::uint16_t code, const char* message);
    void handleWebSocketEvent(std::uint8_t socketId, WStype_t type, std::uint8_t* payload, std::size_t length);

    AsyncWebServer webServer;
    WebSocketsServer socketServer;
    std::uint64_t wifiScanStartedHandlerHandle;
    std::uint64_t wifiScanCompletedHandlerHandle;
    std::uint64_t wifiScanDiscoveryHandlerHandle;
  };
}  // namespace OpenShock
