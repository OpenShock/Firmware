#pragma once

#include <freertos/FreeRTOS.h>

#include "captiveportal/StaticFileServer.h"
#include "captiveportal/WebSocketEndpoint.h"
#include "dns_server/DNSServer.h"
#include "OpenShock.h"

#include <esp_http_server.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace OpenShock::CaptivePortal {
  // Shared so a detached job (account link) can keep the instance, and with it the server, alive until it answers.
  // New references only come from the manager (GetLiveInstance), never from the instance itself: a retired instance
  // is destroyed once the manager's RetiredList holds the only one, which a weak_ptr could otherwise undo.
  class CaptivePortalInstance {
    DISABLE_COPY(CaptivePortalInstance);
    DISABLE_MOVE(CaptivePortalInstance);

  public:
    CaptivePortalInstance();
    ~CaptivePortalInstance();

    inline void retryDrain() { m_webSocket.retryDrain(); }

    inline bool sendMessageTXT(uint8_t socketId, std::string_view data) { return m_webSocket.sendMessageTXT(socketId, data); }
    inline bool sendMessageBIN(uint8_t socketId, std::span<const uint8_t> data) { return m_webSocket.sendMessageBIN(socketId, data); }
    inline bool broadcastMessageTXT(std::string_view data) { return m_webSocket.broadcastMessageTXT(data); }
    inline bool broadcastMessageBIN(std::span<const uint8_t> data) { return m_webSocket.broadcastMessageBIN(data); }
    inline bool hasClients() { return m_webSocket.hasClients(); }

    /// @brief Whether the HTTP and DNS servers came up; a failed instance must not be published.
    inline bool ok() const { return m_server != nullptr && m_dnsStarted; }

  private:
    bool startHttpServer();
    void registerHandlers();

    // The destructor stops the server in its body, so httpd's handlers and callbacks never outlive these members.
    StaticFileServer m_staticFiles;
    WebSocketEndpoint m_webSocket;
    DNSServer m_dnsServer;

    httpd_handle_t m_server = nullptr;
    bool m_dnsStarted       = false;
  };

  /// @brief A reference to `instance` if it is the one the manager has published, or null (being constructed, or
  ///        retired). Defined in Manager.cpp.
  std::shared_ptr<CaptivePortalInstance> GetLiveInstance(const CaptivePortalInstance* instance);
}  // namespace OpenShock::CaptivePortal
