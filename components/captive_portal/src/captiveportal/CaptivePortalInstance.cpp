#include <freertos/FreeRTOS.h>

#include "captiveportal/CaptivePortalInstance.h"

const char* const TAG = "CaptivePortalInstance";

#include "captiveportal/ApiHandlers.h"
#include "captiveportal/Manager.h"
#include "Logging.h"
#include "rfc8908/RFC8908Handler.h"

const uint16_t HTTP_PORT = 80;
// httpd needs 3 lwIP sockets besides its client sockets; the DNS server, the gateway connection and an outbound HTTP request need one each
static_assert(CONFIG_LWIP_MAX_SOCKETS >= CONFIG_OPENSHOCK_PORTAL_HTTPD_MAX_SOCKETS + 3 + 3, "httpd client sockets would leave no lwIP sockets for the DNS server and outbound connections");
static_assert(CONFIG_OPENSHOCK_PORTAL_HTTPD_MAX_SOCKETS > CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS, "every websocket client needs a socket, plus one for plain requests");

using namespace OpenShock;

void CaptivePortal::CaptivePortalInstance::registerHandlers()
{
  // API endpoints (registered before the static wildcard so they take priority).
  RegisterApiHandlers(m_server, this);

  // OS captive-detection probes + RFC 8908 endpoint + 404 redirect (rfc8908 component).
  // Registered before the static wildcard so the specific probe paths take priority.
  RFC8908::RegisterProbeHandlers(m_server, CaptivePortal::ApIPv4String());

  m_webSocket.registerHandler(m_server);

  // Static files: catch-all, registered LAST so specific routes win.
  m_staticFiles.registerHandler(m_server);
}

bool CaptivePortal::CaptivePortalInstance::startHttpServer()
{
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

  // Start the wildcard DNS responder (all A queries -> the portal AP IP).
  m_dnsStarted = m_dnsServer.start(CaptivePortal::ApIPv4String());
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
