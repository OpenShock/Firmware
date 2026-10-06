#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dns_server/DNSServer.h"
#include "DNSPacket.h"

const char* const TAG = "DNSServer";

#include "Logging.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <cerrno>
#include <cstddef>
#include <cstring>

using namespace OpenShock;

DNSServer::DNSServer()
  : m_socket(-1)
  , m_taskHandle(nullptr)
  , m_stop(false)
  , m_taskExited(false)
  , m_ip {}
{
}

DNSServer::~DNSServer()
{
  stop();
}

bool DNSServer::start(const char* responseIpv4, uint16_t port)
{
  if (m_taskHandle != nullptr) {
    return true;  // already running
  }

  // Parse the dotted-decimal address into raw bytes for the answer RDATA.
  // inet_pton rejects out-of-range components and trailing garbage, which a
  // scanf of four ints silently accepts and truncates.
  struct in_addr parsed = {};
  if (responseIpv4 == nullptr || inet_pton(AF_INET, responseIpv4, &parsed) != 1) {
    OS_LOGE(TAG, "Invalid response IPv4: %s", responseIpv4 != nullptr ? responseIpv4 : "(null)");
    return false;
  }
  static_assert(sizeof(parsed.s_addr) == sizeof(m_ip), "s_addr and m_ip must be the same size");
  memcpy(m_ip, &parsed.s_addr, sizeof(m_ip));  // already in network order, i.e. a.b.c.d

  m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (m_socket < 0) {
    OS_LOGE(TAG, "Failed to create DNS socket");
    return false;
  }

  // A receive timeout is the *only* way the task ever gets to look at m_stop:
  // lwIP's shutdown() rejects non-TCP sockets with EOPNOTSUPP, and close()
  // under a blocked recvfrom is undefined without LWIP_NETCONN_FULLDUPLEX.
  // Without this the task would block forever, so a failure here is fatal.
  struct timeval tv = {};
  tv.tv_usec        = 250 * 1000;
  if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    OS_LOGE(TAG, "Failed to set DNS socket receive timeout");
    close(m_socket);
    m_socket = -1;
    return false;
  }

  struct sockaddr_in addr = {};
  addr.sin_family         = AF_INET;
  addr.sin_addr.s_addr    = htonl(INADDR_ANY);
  addr.sin_port           = htons(port);

  if (bind(m_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    OS_LOGE(TAG, "Failed to bind DNS socket to port %u", port);
    close(m_socket);
    m_socket = -1;
    return false;
  }

  m_stop.store(false, std::memory_order_relaxed);
  m_taskExited.store(false, std::memory_order_relaxed);
  if (TaskUtils::TaskCreateExpensive(Util::FnProxy<&DNSServer::task>, "DNSServer", 3072, this, 1, &m_taskHandle) != pdPASS) {
    OS_LOGE(TAG, "Failed to create DNS task");
    close(m_socket);
    m_socket = -1;
    return false;
  }

  return true;
}

void DNSServer::stop()
{
  if (m_taskHandle == nullptr) {
    if (m_socket >= 0) {
      close(m_socket);
      m_socket = -1;
    }
    return;
  }

  m_stop.store(true, std::memory_order_relaxed);

  // The task checks m_stop every time the receive times out, so give it margin
  // over that 250 ms timeout before resorting to a force-kill.
  TaskUtils::StopTask(m_taskHandle, m_taskExited, TAG, "DNSServer task", pdMS_TO_TICKS(2000));
  m_taskHandle = nullptr;

  // Only now is nobody using the fd. Closing it earlier would let lwIP hand
  // the number to another socket while the task was still between calls.
  if (m_socket >= 0) {
    close(m_socket);
    m_socket = -1;
  }
}

void DNSServer::task()
{
  // The reply is the request echoed back with an answer appended, so one
  // buffer serves both directions.
  uint8_t packet[DNSPacket::MAX_PACKET];

  while (!m_stop.load(std::memory_order_relaxed)) {
    struct sockaddr_in from = {};
    socklen_t fromLen       = sizeof(from);

    ssize_t n = recvfrom(m_socket, packet, sizeof(packet), 0, reinterpret_cast<struct sockaddr*>(&from), &fromLen);

    // Don't answer anything once shutdown has begun.
    if (m_stop.load(std::memory_order_relaxed)) {
      break;
    }

    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;  // receive timeout, which is how we get here to check m_stop
      }
      // A socket can enter a persistent error state when the AP interface goes
      // down. Back off so that can't spin the core at full tilt.
      OS_LOGW(TAG, "DNS recvfrom failed: errno=%d", errno);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    size_t respLen = DNSPacket::BuildResponse(packet, static_cast<size_t>(n), sizeof(packet), m_ip);
    if (respLen == 0) {
      continue;  // not a query we answer, or one we cannot safely answer
    }

    if (sendto(m_socket, packet, respLen, 0, reinterpret_cast<struct sockaddr*>(&from), fromLen) < 0) {
      OS_LOGW(TAG, "DNS sendto failed: errno=%d", errno);  // e.g. ENOMEM when TX buffers are exhausted
    }
  }

  TaskUtils::TaskExiting(m_taskExited);
}
