#pragma once

#include <freertos/task.h>

#include <cstdint>

namespace OpenShock::CaptivePortal {
  class CaptiveDNSServer {
  public:
    CaptiveDNSServer(uint32_t ipv4);
    ~CaptiveDNSServer();

    void start();
    void stop();

  private:
    int m_sock;
    uint32_t m_ipv4;
    uint32_t m_ttl;
    TaskHandle_t m_task;
    volatile bool m_running;

    void serverLoop();
  };
}  // namespace OpenShock::CaptivePortal
