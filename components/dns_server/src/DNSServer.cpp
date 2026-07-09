#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dns_server/DNSServer.h"

const char* const TAG = "DNSServer";

#include "Logging.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

#include <lwip/sockets.h>

#include <cstring>

using namespace OpenShock;

DNSServer::DNSServer()
  : m_socket(-1)
  , m_taskHandle(nullptr)
  , m_stop(false)
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
  int a, b, c, d;
  if (responseIpv4 == nullptr || sscanf(responseIpv4, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
    OS_LOGE(TAG, "Invalid response IPv4: %s", responseIpv4 != nullptr ? responseIpv4 : "(null)");
    return false;
  }
  m_ip[0] = static_cast<uint8_t>(a);
  m_ip[1] = static_cast<uint8_t>(b);
  m_ip[2] = static_cast<uint8_t>(c);
  m_ip[3] = static_cast<uint8_t>(d);

  m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (m_socket < 0) {
    OS_LOGE(TAG, "Failed to create DNS socket");
    return false;
  }

  // Short recv timeout so the task can observe stop requests promptly.
  struct timeval tv = {};
  tv.tv_sec         = 1;
  setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

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
  if (m_socket >= 0) {
    shutdown(m_socket, SHUT_RDWR);  // unblock recvfrom
    close(m_socket);
    m_socket = -1;
  }
  TaskUtils::StopTask(m_taskHandle, TAG, "DNSServer task");
  m_taskHandle = nullptr;
}

void DNSServer::task()
{
  uint8_t query[512];
  uint8_t resp[512];

  while (!m_stop.load(std::memory_order_relaxed)) {
    struct sockaddr_in from = {};
    socklen_t fromLen       = sizeof(from);

    int n = recvfrom(m_socket, query, sizeof(query), 0, reinterpret_cast<struct sockaddr*>(&from), &fromLen);
    if (n < 12) {
      continue;  // timeout (-1) or runt packet
    }
    size_t len = static_cast<size_t>(n);

    // Find the end of the question: QNAME (labels terminated by 0) + QTYPE + QCLASS.
    // Use size_t throughout so the bounds arithmetic can't provoke -Wstrict-overflow.
    size_t pos = 12;
    while (pos < len && query[pos] != 0) {
      pos += static_cast<size_t>(query[pos]) + 1;
    }
    if (pos >= len || len - pos < 5) {
      continue;  // malformed
    }
    int qtype          = (query[pos + 1] << 8) | query[pos + 2];
    size_t questionEnd = pos + 5;  // null(1) + qtype(2) + qclass(2)

    memcpy(resp, query, questionEnd);
    resp[2]  = 0x81;  // QR=1, recursion desired copied
    resp[3]  = 0x80;  // RA=1, RCODE=0
    resp[6]  = 0x00;  // ANCOUNT hi
    resp[7]  = (qtype == 1) ? 0x01 : 0x00;
    resp[8]  = 0x00;  // NSCOUNT
    resp[9]  = 0x00;
    resp[10] = 0x00;  // ARCOUNT
    resp[11] = 0x00;

    size_t respLen = questionEnd;
    if (qtype == 1) {  // only answer A queries
      const uint8_t answer[] = {
        0xC0, 0x0C,                          // name pointer → offset 12 (the question)
        0x00, 0x01,                          // TYPE  A
        0x00, 0x01,                          // CLASS IN
        0x00, 0x00, 0x00, 0x00,              // TTL   0
        0x00, 0x04,                          // RDLENGTH 4
        m_ip[0], m_ip[1], m_ip[2], m_ip[3],  // RDATA — the fixed response IP
      };
      memcpy(resp + respLen, answer, sizeof(answer));
      respLen += sizeof(answer);
    }

    sendto(m_socket, resp, respLen, 0, reinterpret_cast<struct sockaddr*>(&from), fromLen);
  }

  vTaskDelete(nullptr);
}
