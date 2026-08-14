#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "OpenShock.h"

#include <atomic>
#include <cstdint>

namespace OpenShock {
  // Minimal captive-portal DNS responder: answers every A query with a fixed IPv4
  // address (and returns an empty answer for other query types) so client operating
  // systems resolve their connectivity-probe hosts to the portal and enter captive
  // mode. Runs a single UDP socket on its own task; not a general-purpose resolver.
  //
  // start() and stop() are not thread-safe against each other or themselves: the
  // socket and task handle are plain members, so the owner must serialize them.
  // The only caller, CaptivePortalInstance, does so by construction (start from
  // its constructor, stop from its destructor).
  class DNSServer {
    DISABLE_COPY(DNSServer);
    DISABLE_MOVE(DNSServer);

  public:
    DNSServer();
    ~DNSServer();

    /// Start answering A queries with responseIpv4 (dotted-decimal, e.g. "4.3.2.1").
    /// Idempotent, first call wins: if the server is already running this returns
    /// true and keeps the address and port it was originally started with.
    bool start(const char* responseIpv4, uint16_t port = 53);
    void stop();

    bool isRunning() const { return m_taskHandle != nullptr; }

  private:
    void task();

    int m_socket;
    TaskHandle_t m_taskHandle;
    std::atomic<bool> m_stop;
    uint8_t m_ip[4];
  };
}  // namespace OpenShock
