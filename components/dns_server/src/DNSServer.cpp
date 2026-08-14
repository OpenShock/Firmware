#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dns_server/DNSServer.h"

const char* const TAG = "DNSServer";

#include "Logging.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <cerrno>
#include <cstddef>
#include <cstring>

namespace {
  constexpr size_t DNS_HEADER_SIZE  = 12;
  constexpr size_t DNS_MAX_PACKET   = 512;
  constexpr size_t DNS_MAX_NAME_LEN = 255;
  constexpr uint32_t DNS_ANSWER_TTL = 60;  // seconds; a zero TTL makes clients re-query on every probe

  // Header flag bits, as a big-endian uint16 read from bytes [2..3].
  constexpr uint16_t DNS_FLAG_QR     = 0x8000;  // set on responses
  constexpr uint16_t DNS_FLAG_AA     = 0x0400;  // authoritative answer
  constexpr uint16_t DNS_FLAG_RD     = 0x0100;  // recursion desired
  constexpr uint16_t DNS_OPCODE_MASK = 0x7800;

  constexpr uint16_t DNS_TYPE_A   = 1;
  constexpr uint16_t DNS_CLASS_IN = 1;

  // Walks the QNAME at `data` and returns its encoded length including the
  // terminating zero byte, or 0 if the name is malformed, compressed, or
  // longer than a name may legally be.
  //
  // Compression pointers are rejected rather than accepted: in a question
  // there is nothing valid for one to point back at, and echoing it would
  // leave the answer's own 0xC00C pointer aimed at a dangling chain.
  size_t ParseQName(const uint8_t* data, size_t avail)
  {
    size_t offset = 0;

    while (offset < avail) {
      uint8_t label = data[offset];

      if (label == 0) {
        return offset + 1;  // consume the terminator
      }

      // Only ordinary labels are accepted: 0b11 marks a compression pointer
      // and 0b01/0b10 are reserved, neither of which is a length.
      if ((label & 0xC0) != 0) {
        return 0;
      }

      if (offset + 1 + label > avail) {
        return 0;  // label overruns the packet
      }

      offset += static_cast<size_t>(label) + 1;

      // Leave room for the terminator, so the encoded name including it stays
      // within DNS_MAX_NAME_LEN.
      if (offset >= DNS_MAX_NAME_LEN) {
        return 0;
      }
    }

    return 0;  // ran off the end without a terminator
  }
}  // namespace

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
  TaskUtils::StopTask(m_taskHandle, TAG, "DNSServer task", pdMS_TO_TICKS(2000));
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
  uint8_t packet[DNS_MAX_PACKET];

  // The answer's owner name is a compression pointer to the question, which
  // always begins immediately after the header.
  static_assert(DNS_HEADER_SIZE == 0x0C, "the answer's 0xC00C pointer assumes the question starts at offset 12");

  // Depends only on m_ip, which is fixed before this task starts.
  // clang-format off
  const uint8_t answer[] = {
    0xC0, 0x0C,                                  // name pointer → offset 12 (the question)
    0x00, 0x01,                                  // TYPE  A
    0x00, 0x01,                                  // CLASS IN
    static_cast<uint8_t>(DNS_ANSWER_TTL >> 24),  // TTL
    static_cast<uint8_t>(DNS_ANSWER_TTL >> 16),
    static_cast<uint8_t>(DNS_ANSWER_TTL >> 8),
    static_cast<uint8_t>(DNS_ANSWER_TTL),
    0x00, 0x04,                                  // RDLENGTH 4
    m_ip[0], m_ip[1], m_ip[2], m_ip[3],          // RDATA — the fixed response IP
  };
  // clang-format on

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

    size_t len = static_cast<size_t>(n);
    if (len < DNS_HEADER_SIZE) {
      continue;  // runt packet
    }

    // A datagram that exactly fills the buffer was almost certainly truncated
    // by recvfrom, and we would be answering a question we never fully saw. No
    // legitimate query comes close: 12 + 255 + 4 is the most one can be.
    if (len == sizeof(packet)) {
      continue;
    }

    uint16_t flags = static_cast<uint16_t>((packet[2] << 8) | packet[3]);

    // Ignore responses — answering one lets two of these servers keep each
    // other busy indefinitely, and makes us usable as a reflector.
    if ((flags & DNS_FLAG_QR) != 0) {
      continue;
    }

    // Only standard queries. Any other opcode would otherwise get a NOERROR
    // reply, carrying its own opcode back, for a request we never handled.
    if ((flags & DNS_OPCODE_MASK) != 0) {
      continue;
    }

    // A captive portal only ever needs to answer a single question.
    uint16_t qdcount = static_cast<uint16_t>((packet[4] << 8) | packet[5]);
    if (qdcount != 1) {
      continue;
    }

    size_t qnameLen = ParseQName(packet + DNS_HEADER_SIZE, len - DNS_HEADER_SIZE);
    if (qnameLen == 0) {
      continue;  // malformed, compressed or oversized name
    }

    // QTYPE(2) and QCLASS(2) follow the name and must both be present.
    if (len - DNS_HEADER_SIZE - qnameLen < 4) {
      continue;
    }
    size_t questionEnd = DNS_HEADER_SIZE + qnameLen + 4;

    const uint8_t* qfixed = packet + DNS_HEADER_SIZE + qnameLen;
    uint16_t qtype        = static_cast<uint16_t>((qfixed[0] << 8) | qfixed[1]);
    uint16_t qclass       = static_cast<uint16_t>((qfixed[2] << 8) | qfixed[3]);

    bool answering = qtype == DNS_TYPE_A && qclass == DNS_CLASS_IN;

    // A question that fills the buffer leaves no room for the answer; without
    // this the memcpy below would run past the end of packet.
    size_t respLen = questionEnd + (answering ? sizeof(answer) : 0);
    if (respLen > sizeof(packet)) {
      OS_LOGW(TAG, "Query leaves no room for a response (%zu bytes), ignoring", respLen);
      continue;
    }

    // Reply in place — the transaction ID and question are already correct.
    uint16_t respFlags = DNS_FLAG_QR | DNS_FLAG_AA | (flags & DNS_FLAG_RD);
    packet[2]          = static_cast<uint8_t>(respFlags >> 8);
    packet[3]          = static_cast<uint8_t>(respFlags);
    packet[6]          = 0x00;  // ANCOUNT hi
    packet[7]          = answering ? 0x01 : 0x00;
    packet[8]          = 0x00;  // NSCOUNT
    packet[9]          = 0x00;
    packet[10]         = 0x00;  // ARCOUNT
    packet[11]         = 0x00;

    if (answering) {
      memcpy(packet + questionEnd, answer, sizeof(answer));
    }

    if (sendto(m_socket, packet, respLen, 0, reinterpret_cast<struct sockaddr*>(&from), fromLen) < 0) {
      OS_LOGW(TAG, "DNS sendto failed: errno=%d", errno);  // e.g. ENOMEM when TX buffers are exhausted
    }
  }

  vTaskDelete(nullptr);
}
