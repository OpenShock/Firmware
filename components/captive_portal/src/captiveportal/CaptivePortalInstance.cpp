#include <freertos/FreeRTOS.h>

#include "captiveportal/CaptivePortalInstance.h"

const char* const TAG = "CaptivePortalInstance";

#include "captiveportal/ApiHandlers.h"
#include "captiveportal/Manager.h"
#include "Logging.h"
#include "rfc8908/RFC8908Handler.h"

#include <lwip/sockets.h>

#include <cstdint>
#include <cstring>

const uint16_t HTTP_PORT = 80;
// httpd needs 3 lwIP sockets besides its client sockets; the DNS server, the gateway connection and an outbound HTTP request need one each
static_assert(CONFIG_LWIP_MAX_SOCKETS >= CONFIG_OPENSHOCK_PORTAL_HTTPD_MAX_SOCKETS + 3 + 3, "httpd client sockets would leave no lwIP sockets for the DNS server and outbound connections");
static_assert(CONFIG_OPENSHOCK_PORTAL_HTTPD_MAX_SOCKETS > CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS, "every websocket client needs a socket, plus one for plain requests");

using namespace OpenShock;

void CaptivePortal::CaptivePortalInstance::registerHandlers()
{
  // API endpoints (registered before the static wildcard so they take priority).
  RegisterApiHandlers(m_server, this);

  // RFC 8908 endpoint (rfc8908 component); probes fall through to the static handler's redirect.
  // Registered before the static wildcard so the API paths take priority.
  RFC8908::RegisterProbeHandlers(m_server, CaptivePortal::ApIPv4String());

  m_webSocket.registerHandler(m_server);

  // Static files: catch-all, registered LAST so specific routes win.
  m_staticFiles.registerHandler(m_server);
}

// The portal AP address in network byte order, set before the server starts
static in_addr_t s_apAddr = 0;

// True when the connection came in over the portal AP: its local address is the AP's. A client on the hub's LAN
// connects to the STA address instead.
static bool arrivedOnAp(int sockfd)
{
  sockaddr_storage local = {};
  socklen_t len          = sizeof(local);
  if (getsockname(sockfd, reinterpret_cast<sockaddr*>(&local), &len) != 0) {
    return false;
  }

  in_addr_t addr;
  if (local.ss_family == AF_INET) {
    addr = reinterpret_cast<const sockaddr_in*>(&local)->sin_addr.s_addr;
  } else if (local.ss_family == AF_INET6) {
    // httpd listens on a dual-stack IPv6 socket when lwIP has IPv6, so an IPv4 client shows up as ::ffff:a.b.c.d
    static constexpr uint8_t kV4MappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    const in6_addr& addr6                        = reinterpret_cast<const sockaddr_in6*>(&local)->sin6_addr;
    if (memcmp(addr6.s6_addr, kV4MappedPrefix, sizeof(kV4MappedPrefix)) != 0) {
      return false;
    }
    memcpy(&addr, addr6.s6_addr + sizeof(kV4MappedPrefix), sizeof(addr));
  } else {
    return false;
  }

  return addr == s_apAddr;
}

// httpd turns Nagle off only around error responses. A websocket frame goes out as two writes (header, then payload),
// and Nagle holds the second until the first is acknowledged, which a delayed-ACK client sends up to ~200 ms later.
static esp_err_t onSessionOpen(httpd_handle_t /*server*/, int sockfd)
{
  // httpd always listens on every interface and has no bind address, so the LAN is refused here instead. The portal's
  // API is the local protocol, which has no compatibility rules: nothing on the LAN may come to depend on it.
  if (!arrivedOnAp(sockfd)) {
    OS_LOGW(TAG, "Refusing a connection that did not come in over the portal AP (fd %d)", sockfd);
    return ESP_FAIL;  // httpd closes the socket
  }

  int one = 1;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0) {
    OS_LOGW(TAG, "Failed to set TCP_NODELAY on fd %d (errno %d)", sockfd, errno);
  }
  return ESP_OK;  // Still usable, just slower
}

bool CaptivePortal::CaptivePortalInstance::startHttpServer()
{
  in_addr apAddr = {};
  if (inet_pton(AF_INET, CaptivePortal::ApIPv4String(), &apAddr) != 1) {
    OS_LOGE(TAG, "Invalid portal AP address: %s", CaptivePortal::ApIPv4String());
    return false;
  }
  s_apAddr = apAddr.s_addr;

  httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
  config.server_port      = HTTP_PORT;
  config.max_uri_handlers = 40;
  config.max_open_sockets = CONFIG_OPENSHOCK_PORTAL_HTTPD_MAX_SOCKETS;
  config.lru_purge_enable = true;
  config.stack_size       = CONFIG_OPENSHOCK_PORTAL_HTTPD_STACK_SIZE;
  // Shorter than the default 10 s: one stalled or sleeping client blocks the single httpd task for this long.
  config.recv_wait_timeout = CONFIG_OPENSHOCK_PORTAL_HTTPD_TIMEOUT_S;
  config.send_wait_timeout = CONFIG_OPENSHOCK_PORTAL_HTTPD_TIMEOUT_S;
  config.uri_match_fn      = httpd_uri_match_wildcard;
  config.open_fn           = onSessionOpen;
  // Same core as the WiFi/lwIP tasks, away from the E-Stop and RF tasks on core 1
  config.core_id = 0;

  esp_err_t err = httpd_start(&m_server, &config);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    m_server = nullptr;
    return false;
  }

  registerHandlers();
  return true;
}

CaptivePortal::CaptivePortalInstance::CaptivePortalInstance()
{
  // Start the combined HTTP + WebSocket server on port 80.
  if (!startHttpServer()) {
    return;
  }

  // Start the wildcard DNS responder (all A queries -> the portal AP IP), on the AP address only: the hub's LAN
  // address must not answer DNS.
  m_dnsStarted = m_dnsServer.start(CaptivePortal::ApIPv4String(), CaptivePortal::ApIPv4String());
  if (!m_dnsStarted) {
    OS_LOGE(TAG, "Failed to start DNS server");
  }
}

CaptivePortal::CaptivePortalInstance::~CaptivePortalInstance()
{
  m_dnsServer.stop();

  // No account link can still be answering on this server: its job holds a reference, so this only runs after it.

  // Stop the server (closes all WS sockets, firing the session free callbacks).
  if (m_server != nullptr) {
    httpd_stop(m_server);
    m_server = nullptr;
  }
}
