#include <freertos/FreeRTOS.h>

#include "captiveportal/CaptiveDNSServer.h"

const char* const TAG = "CaptiveDNSServer";

#include "Logging.h"
#include "util/FnProxy.h"
#include "util/TaskUtils.h"

#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <cstdio>
#include <cstring>

#define DNS_PORT            53
#define DNS_DEFAULT_TTL     60
#define MAX_DNS_PACKET_SIZE 512
#define MAX_DOMAIN_LEN      255
#define UDP_RECV_TIMEOUT_MS 200  // 200 ms

// DNS flag fields
constexpr uint16_t DNS_FLAG_QR       = 0x8000;  // Query/Response
constexpr uint16_t DNS_FLAG_AA       = 0x0400;  // Authoritative Answer
constexpr uint16_t DNS_FLAG_TC       = 0x0200;  // Truncation
constexpr uint16_t DNS_FLAG_RD       = 0x0100;  // Recursion Desired
constexpr uint16_t DNS_FLAG_RA       = 0x0080;  // Recursion Available
constexpr uint16_t DNS_FLAG_AD       = 0x0020;  // Authenticated Data
constexpr uint16_t DNS_FLAG_CD       = 0x0010;  // Checking Disabled
constexpr uint16_t DNS_OPCODE_MASK   = 0x7800;  // Operation Code
constexpr uint16_t DNS_ZERO_MASK     = 0x0040;  // Zero Bytes
constexpr uint16_t DNS_RESPCODE_MASK = 0x000F;  // Response Code

enum class DnsOpcode : uint8_t {
  Query  = 0,
  IQuery = 1,
  Status = 2,
  Notify = 4,
  Update = 5
};

enum class DNSReplyCode : uint8_t {
  NoError           = 0,
  FormError         = 1,
  ServerFailure     = 2,
  NonExistentDomain = 3,
  NotImplemented    = 4,
  Refused           = 5
};

enum class DNSType : uint16_t {
  A = 1,
};

enum class DNSClass : uint16_t {
  IN = 1
};

// DNS Header (12 bytes)
struct DNSHeader {
  uint16_t ID;       // Transaction ID
  uint16_t flags;    // Bitfields packed
  uint16_t QDCount;  // Questions
  uint16_t ANCount;  // Answers
  uint16_t NSCount;  // Authority
  uint16_t ARCount;  // Additional
} __attribute__((packed));

// Answer section (compressed name pointer, type, class, TTL, RDLENGTH, RDATA)
struct DNSAnswer {
  uint16_t Name;  // usually pointer to QName (0xC00C)
  uint16_t Type;
  uint16_t Class;
  uint32_t TTL;
  uint16_t RDLength;
  uint32_t RData;  // IPv4 address
} __attribute__((packed));

using namespace OpenShock::CaptivePortal;

// Precompute base response flags (QR and AA are always set)
static constexpr uint16_t BASE_RESPONSE_FLAGS = DNS_FLAG_QR | DNS_FLAG_AA;
static constexpr uint16_t FLAGS_TO_ECHO       = DNS_FLAG_RD | DNS_OPCODE_MASK;

// Minimum valid packet size: header + at least 1 byte name + null terminator + type + class
static constexpr size_t MIN_QUESTION_SIZE = sizeof(DNSHeader) + 5;

static constexpr uint16_t buildResponseFlags(uint16_t requestFlags)
{
  return BASE_RESPONSE_FLAGS | (requestFlags & FLAGS_TO_ECHO);
}

static inline bool isValidDnsQuery(const DNSHeader* hdr, int packetLen)
{
  if (packetLen < static_cast<int>(sizeof(DNSHeader))) return false;
  if (ntohs(hdr->flags) & DNS_FLAG_QR) return false;  // Ignore responses
  if (ntohs(hdr->QDCount) != 1) return false;         // Only single question supported
  return true;
}

static inline DNSAnswer buildDnsAnswer(uint32_t ipv4, uint32_t ttl)
{
  DNSAnswer answer;

  memset(&answer, 0, sizeof(answer));

  answer.Name     = htons(0xC000 | sizeof(DNSHeader));  // Pointer to question name
  answer.Type     = htons(static_cast<uint16_t>(DNSType::A));
  answer.Class    = htons(static_cast<uint16_t>(DNSClass::IN));
  answer.TTL      = htonl(ttl);
  answer.RDLength = htons(4);  // IPv4 address length
  answer.RData    = ipv4;

  return answer;
}

static int extractQName(uint8_t* data, uint8_t* end)
{
  size_t offset = 0;

  while (offset < MAX_DOMAIN_LEN && data + offset < end) {
    uint8_t len = data[offset];

    if (len == 0) {
      offset++;  // Include terminating zero
      break;
    }

    if (len & 0xC0) {
      // Handle pointer compression in question section
      if (offset + 1 >= static_cast<size_t>(end - data)) {
        OS_LOGW(TAG, "Malformed QName pointer, offset %zu", offset);
        return 0;
      }
      offset += 2;  // Pointer consumes two bytes
      break;
    }

    if (offset + 1 + len > static_cast<size_t>(end - data)) {
      OS_LOGW(TAG, "QName label overruns buffer, offset %zu, label len %d", offset, len);
      return 0;
    }

    offset += len + 1;
  }

  if (offset >= MAX_DOMAIN_LEN) {
    OS_LOGW(TAG, "QName too long (%zu bytes)", offset);
    return 0;
  }

  return static_cast<int>(offset);
}

static bool handleDnsPacket(int sock, uint8_t* buffer, int length, const sockaddr_in& client_addr, socklen_t client_len, uint32_t ipv4, uint32_t ttl)
{
  DNSHeader* hdr = reinterpret_cast<DNSHeader*>(buffer);

  // Validate query
  if (!isValidDnsQuery(hdr, length)) {
    if (length >= static_cast<int>(sizeof(DNSHeader))) {
      uint16_t flags = ntohs(hdr->flags);
      if (flags & DNS_FLAG_QR) {
        OS_LOGW(TAG, "Received DNS response packet, ignoring");
      } else if (ntohs(hdr->QDCount) != 1) {
        OS_LOGW(TAG, "Unexpected number of questions: %d, only single question supported", ntohs(hdr->QDCount));
      }
    } else {
      OS_LOGW(TAG, "Received packet too short (%d bytes), ignoring", length);
    }
    return false;
  }

  // Check minimum size for question section
  if (length < static_cast<int>(MIN_QUESTION_SIZE)) {
    OS_LOGW(TAG, "Packet too short for question section, length: %d", length);
    return false;
  }

  // Extract QName
  int qname_len = extractQName(buffer + sizeof(DNSHeader), buffer + length);
  if (qname_len == 0) {
    OS_LOGW(TAG, "Malformed or too long QName in query, ignoring");
    return false;
  }

  int question_len = qname_len + 4;  // QName + QType (2) + QClass (2)
  if (sizeof(DNSHeader) + question_len > static_cast<size_t>(length)) {
    OS_LOGW(TAG, "Packet length %d smaller than expected question length %zu", length, sizeof(DNSHeader) + question_len);
    return false;
  }

  // Ensure response won't overflow buffer
  size_t total_response_size = sizeof(DNSHeader) + question_len + sizeof(DNSAnswer);
  if (total_response_size > MAX_DNS_PACKET_SIZE) {
    OS_LOGE(TAG, "DNS response would overflow buffer, skipping");
    return false;
  }

  // Build response header in-place
  hdr->flags   = htons(buildResponseFlags(ntohs(hdr->flags)));
  hdr->ANCount = htons(1);
  hdr->NSCount = 0;
  hdr->ARCount = 0;

  // Build and append answer
  DNSAnswer answer = buildDnsAnswer(ipv4, ttl);
  memcpy(buffer + sizeof(DNSHeader) + question_len, &answer, sizeof(DNSAnswer));

  // Send response
  int sent = sendto(sock, buffer, total_response_size, 0, reinterpret_cast<const sockaddr*>(&client_addr), client_len);
  if (sent < 0) {
    OS_LOGW(TAG, "Failed to send DNS response: %d (%s)", errno, strerror(errno));
    return false;
  }

  if (sent != static_cast<int>(total_response_size)) {
    OS_LOGW(TAG, "Partial DNS response sent (%d/%zu bytes)", sent, total_response_size);
    return false;
  }

  return true;
}

CaptiveDNSServer::CaptiveDNSServer(uint32_t ipv4)
  : m_sock(-1)
  , m_ipv4(ipv4)
  , m_ttl(DNS_DEFAULT_TTL)
  , m_task(nullptr)
  , m_running(false)
{
  OS_LOGD(TAG, "Creating CaptiveDNSServer");
}

CaptiveDNSServer::~CaptiveDNSServer()
{
  vTaskDelete(m_task);

  if (m_sock >= 0) {
    close(m_sock);
  }
}

void CaptiveDNSServer::start()
{
  if (m_running || m_task != nullptr) {
    OS_LOGW(TAG, "DNS server already running");
    return;
  }

  OS_LOGI(TAG, "Starting DNS server");

  m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (m_sock < 0) {
    OS_LOGE(TAG, "Failed to create socket: %d", errno);
    return;
  }

  struct timeval timeout;
  timeout.tv_sec  = 0;
  timeout.tv_usec = UDP_RECV_TIMEOUT_MS * 1000;
  if (setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    OS_LOGW(TAG, "Failed to set socket timeout: %d (%s)", errno, strerror(errno));
  }

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons(DNS_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(m_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    OS_LOGE(TAG, "Failed to bind socket to port %d: %d", DNS_PORT, errno);
    stop();
    return;
  }

  m_running = true;
  OS_LOGI(TAG, "DNS server bound successfully, creating task");

  BaseType_t result = TaskUtils::TaskCreateUniversal(&Util::FnProxy<&CaptiveDNSServer::serverLoop>, "dns_task", 4096, this, 5, &m_task, 1);
  if (result != pdPASS) {
    OS_LOGE(TAG, "Failed to create DNS task");
    stop();
    return;
  }
}

void CaptiveDNSServer::stop()
{
  if (!m_running) return;

  m_running = false;

  if (m_sock >= 0) {
    shutdown(m_sock, SHUT_RDWR);  // unblock recvfrom()
  }

  // Wait for the server task to notify us that it's exiting
  if (m_task != nullptr) {
    ESP_LOGD(TAG, "DELETING...");
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));  // wait up to 1 second
    ESP_LOGD(TAG, "DELETING!");
    vTaskDelete(m_task);
    ESP_LOGD(TAG, "DELETED!");
  }

  // Now it's safe to close the socket
  if (m_sock >= 0) {
    close(m_sock);
    m_sock = -1;
  }

  m_task = nullptr;
}

void CaptiveDNSServer::serverLoop()
{
  sockaddr_in client_addr;
  uint8_t recvbuf[MAX_DNS_PACKET_SIZE];

  while (m_running) {
    socklen_t client_len = sizeof(client_addr);

    int len = recvfrom(m_sock, recvbuf, MAX_DNS_PACKET_SIZE, 0, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

    if (len < 0) {
      if (!m_running) break;
      if (errno == EWOULDBLOCK || errno == EAGAIN) continue;

      OS_LOGE(TAG, "Error receiving DNS packet: %d (%s)", errno, strerror(errno));

      continue;  // Continue instead of break to keep server alive
    }

    handleDnsPacket(m_sock, recvbuf, len, client_addr, client_len, m_ipv4, m_ttl);
  }

  // Notify stop() that the task is exiting
  ESP_LOGD(TAG, "EXITING...");
  xTaskNotifyGive(m_task);
  ESP_LOGD(TAG, "EXITING!");
}
