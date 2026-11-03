#include <freertos/FreeRTOS.h>

#include "captiveportal/WebSocketEndpoint.h"

const char* const TAG = "CaptivePortalWs";

#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "message_handlers/WebSocket.h"
#include "serialization/WSLocal.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include <utility>

using namespace OpenShock;

void CaptivePortal::WebSocketEndpoint::registerHandler(httpd_handle_t server)
{
  m_server = server;

  httpd_uri_t ws              = {};
  ws.uri                      = "/ws";
  ws.method                   = HTTP_GET;
  ws.handler                  = &WebSocketEndpoint::wsHandler;
  ws.user_ctx                 = this;
  ws.is_websocket             = true;
  ws.handle_ws_control_frames = false;
  ws.supported_subprotocol    = "flatbuffers";
  ws.ws_post_handshake_cb     = &WebSocketEndpoint::wsPostHandshake;
  if (httpd_register_uri_handler(m_server, &ws) != ESP_OK) {
    OS_LOGE(TAG, "Failed to register WebSocket handler");
  }
}

namespace {
  // Per-session context so httpd's free callback can reap the fd->id slot on close.
  struct WsSessCtx {
    OpenShock::CaptivePortal::WebSocketEndpoint* self;
    int fd;
  };

#if defined(CONFIG_HTTPD_QUEUE_WORK_BLOCKING)
  // httpd_queue_work is called from the httpd task itself (drainSends); a blocking mailbox would deadlock it
#error "CONFIG_HTTPD_QUEUE_WORK_BLOCKING must be disabled"
#endif

  void sendToWsClient(httpd_handle_t hd, int fd, httpd_ws_frame_t& frame)
  {
    if (httpd_ws_send_frame_async(hd, fd, &frame) != ESP_OK) {
      // A client that can't take data (gone, asleep, buffers full) would otherwise stall every later send for the
      // full send timeout; drop it, the web UI reconnects.
      httpd_sess_trigger_close(hd, fd);
      return;
    }

    // httpd only counts inbound requests as activity, so a push-only WebSocket would look idle to the LRU purge.
    httpd_sess_update_lru_counter(hd, fd);
  }

}  // namespace

int CaptivePortal::WebSocketEndpoint::idForFd(int fd)
{
  ScopedLock lock__(&m_clientsMutex);
  for (uint8_t i = 0; i < CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS; ++i) {
    if (m_clients[i].used && m_clients[i].fd == fd) {
      return i;
    }
  }
  return -1;
}

int CaptivePortal::WebSocketEndpoint::fdForId(uint8_t socketId)
{
  ScopedLock lock__(&m_clientsMutex);
  if (socketId < CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS && m_clients[socketId].used) {
    return m_clients[socketId].fd;
  }
  return -1;
}

uint8_t CaptivePortal::WebSocketEndpoint::onWsOpen(int fd)
{
  ScopedLock lock__(&m_clientsMutex);

  // Idempotent: an fd already tracked keeps its id.
  for (uint8_t i = 0; i < CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS; ++i) {
    if (m_clients[i].used && m_clients[i].fd == fd) {
      return i;
    }
  }

  for (uint8_t i = 0; i < CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS; ++i) {
    if (!m_clients[i].used) {
      m_clients[i].used        = true;
      m_clients[i].fd          = fd;
      m_clients[i].reasmActive = false;
      m_clients[i].reasmType   = WebSocketMessageType::Binary;
      m_clients[i].reasm.clear();
      return i;
    }
  }

  return 0xFF;  // full
}

void CaptivePortal::WebSocketEndpoint::onWsClose(int fd)
{
  int id = -1;
  {
    ScopedLock lock__(&m_clientsMutex);
    for (uint8_t i = 0; i < CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS; ++i) {
      if (m_clients[i].used && m_clients[i].fd == fd) {
        m_clients[i].used        = false;
        m_clients[i].reasmActive = false;
        m_clients[i].reasm.clear();
        id = i;
        break;
      }
    }
  }

  if (id >= 0) {
    handleWebSocketClientDisconnected(static_cast<uint8_t>(id));
  }
}

void CaptivePortal::WebSocketEndpoint::wsSessCtxFree(void* ctx)
{
  auto* c = static_cast<WsSessCtx*>(ctx);
  if (c == nullptr) {
    return;
  }
  c->self->onWsClose(c->fd);
  delete c;
}

// httpd does not call the URI handler for the handshake (esp_http_server's httpd_uri.c), only this callback, right
// after it has switched the socket to WebSocket: allocate an id, arm the disconnect hook and greet the client.
esp_err_t CaptivePortal::WebSocketEndpoint::wsPostHandshake(httpd_req_t* req)
{
  auto* self = static_cast<WebSocketEndpoint*>(req->user_ctx);
  int fd     = httpd_req_to_sockfd(req);

  uint8_t id = self->onWsOpen(fd);
  if (id == 0xFF) {
    OS_LOGW(TAG, "WebSocket client table full, rejecting fd %d", fd);
    httpd_sess_trigger_close(self->m_server, fd);
    return ESP_OK;
  }

  auto* sessCtx = new WsSessCtx {self, fd};
  httpd_sess_set_ctx(self->m_server, fd, sessCtx, &WebSocketEndpoint::wsSessCtxFree);

  self->handleWebSocketClientConnected(req, id);
  return ESP_OK;
}

esp_err_t CaptivePortal::WebSocketEndpoint::wsHandler(httpd_req_t* req)
{
  auto* self = static_cast<WebSocketEndpoint*>(req->user_ctx);
  int fd     = httpd_req_to_sockfd(req);

  // Data frame: two-call recv (length first, then payload).
  httpd_ws_frame_t frame = {};
  esp_err_t ret          = httpd_ws_recv_frame(req, &frame, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  if (frame.len == 0) {
    self->onWsFrame(fd, frame.type, frame.final, {});
    return ESP_OK;
  }

  if (frame.len > CONFIG_OPENSHOCK_LOCAL_MAX_MESSAGE_SIZE) {  // Nothing larger is accepted by the local message handler, so don't buffer it
    OS_LOGE(TAG, "WebSocket frame too large (%u bytes), dropping", static_cast<unsigned>(frame.len));
    return ESP_FAIL;
  }

  std::vector<uint8_t> buf(frame.len);
  frame.payload = buf.data();
  ret           = httpd_ws_recv_frame(req, &frame, frame.len);
  if (ret != ESP_OK) {
    return ret;
  }

  self->onWsFrame(fd, frame.type, frame.final, {buf.data(), frame.len});
  return ESP_OK;
}

void CaptivePortal::WebSocketEndpoint::onWsFrame(int fd, httpd_ws_type_t opcode, bool final, std::span<const uint8_t> payload)
{
  int idx = idForFd(fd);
  if (idx < 0) {
    // Defensive: wsPostHandshake() registers every client, so a frame from an unknown fd should not happen.
    uint8_t id = onWsOpen(fd);
    if (id == 0xFF) {
      return;
    }
    idx = id;
  }
  uint8_t socketId = static_cast<uint8_t>(idx);

  switch (opcode) {
    case HTTPD_WS_TYPE_TEXT:
    case HTTPD_WS_TYPE_BINARY:
    {
      WebSocketMessageType type = (opcode == HTTPD_WS_TYPE_TEXT) ? WebSocketMessageType::Text : WebSocketMessageType::Binary;
      {
        ScopedLock lock__(&m_clientsMutex);
        if (m_clients[idx].reasmActive) {
          // A new message started before the previous fragmented one finished; drop the stale fragments
          OS_LOGW(TAG, "WebSocket client %u started a new message mid-fragment, dropping the old one", socketId);
          m_clients[idx].reasmActive = false;
          m_clients[idx].reasm.clear();
        }
        if (!final) {
          m_clients[idx].reasmActive = true;
          m_clients[idx].reasmType   = type;
          m_clients[idx].reasm.assign(payload.begin(), payload.end());
          break;
        }
      }
      dispatchWsMessage(socketId, type, payload);
      break;
    }
    case HTTPD_WS_TYPE_CONTINUE:
    {
      WebSocketMessageType type;
      std::vector<uint8_t> full;
      {
        ScopedLock lock__(&m_clientsMutex);
        if (!m_clients[idx].reasmActive) {
          OS_LOGW(TAG, "WebSocket client %u sent a continuation frame without a message start, dropping it", socketId);
          break;
        }
        // The frame check only bounds single frames; also bound the reassembled message
        if (m_clients[idx].reasm.size() + payload.size() > CONFIG_OPENSHOCK_LOCAL_MAX_MESSAGE_SIZE) {
          OS_LOGE(TAG, "WebSocket client %u fragmented message exceeds %d bytes, closing", socketId, CONFIG_OPENSHOCK_LOCAL_MAX_MESSAGE_SIZE);
          m_clients[idx].reasmActive = false;
          m_clients[idx].reasm.clear();
          m_clients[idx].reasm.shrink_to_fit();
          httpd_sess_trigger_close(m_server, fd);
          break;
        }
        m_clients[idx].reasm.insert(m_clients[idx].reasm.end(), payload.begin(), payload.end());
        if (!final) {
          break;
        }
        type                       = m_clients[idx].reasmType;
        full                       = std::move(m_clients[idx].reasm);
        m_clients[idx].reasmActive = false;
        m_clients[idx].reasm.clear();
      }
      dispatchWsMessage(socketId, type, full);
      break;
    }
    default:
      // PING/PONG/CLOSE are handled by httpd (handle_ws_control_frames = false).
      break;
  }
}

void CaptivePortal::WebSocketEndpoint::dispatchWsMessage(uint8_t socketId, WebSocketMessageType type, std::span<const uint8_t> payload)
{
  switch (type) {
    case WebSocketMessageType::Binary:
      MessageHandlers::WebSocket::HandleLocalBinary(socketId, payload);
      break;
    case WebSocketMessageType::Text:
      OS_LOGE(TAG, "Text messages are not supported");
      break;
    default:
      break;
  }
}

void CaptivePortal::WebSocketEndpoint::handleWebSocketClientConnected(httpd_req_t* req, uint8_t socketId)
{
  OS_LOGD(TAG, "WebSocket client #%u connected (fd %d)", socketId, httpd_req_to_sockfd(req));

  // We're on the httpd task with a live req; send directly, no copy/queue needed.
  auto sendBin = [req](std::span<const uint8_t> data) -> bool {
    httpd_ws_frame_t frame = {};
    frame.final            = true;
    frame.type             = HTTPD_WS_TYPE_BINARY;
    frame.payload          = const_cast<uint8_t*>(data.data());
    frame.len              = data.size();
    return httpd_ws_send_frame(req, &frame) == ESP_OK;
  };

  WiFiNetwork connectedNetwork;
  WiFiNetwork* connectedNetworkPtr = nullptr;
  if (WiFiManager::GetConnectedNetwork(connectedNetwork)) {
    connectedNetworkPtr = &connectedNetwork;
  }

  Serialization::Local::SerializeReadyMessage(connectedNetworkPtr, GatewayConnectionManager::IsLinked(), sendBin);

  // Send all previously scanned wifi networks
  auto networks = OpenShock::WiFiManager::GetDiscoveredWiFiNetworks();
  Serialization::Local::SerializeWiFiNetworksEvent(Serialization::Types::WifiNetworkEventType::Discovered, networks, sendBin);

  // Nothing scanned yet (e.g. connected at boot, so no auto-scan ran): start one so the list isn't empty
  if (networks.empty() && !WiFiScanManager::IsScanning()) {
    WiFiScanManager::StartScan();
  }
}

void CaptivePortal::WebSocketEndpoint::handleWebSocketClientDisconnected(uint8_t socketId)
{
  OS_LOGD(TAG, "WebSocket client #%u disconnected", socketId);
}

// ---------------------------------------------------------------------------
// Public WS send API (called from arbitrary tasks -> must copy + queue)
// ---------------------------------------------------------------------------

bool CaptivePortal::WebSocketEndpoint::queueSend(int fd, bool broadcast, bool binary, std::span<const uint8_t> data)
{
  if (m_server == nullptr) {
    return false;
  }

  {
    ScopedLock lock__(&m_sendMutex);
    if (m_sendQueue.size() >= CONFIG_OPENSHOCK_PORTAL_WS_MAX_QUEUED_SENDS) {
      OS_LOGW(TAG, "WebSocket send queue full, dropping message");
      return false;
    }
    m_sendQueue.push_back(WsSend {.fd = broadcast ? -1 : fd, .binary = binary, .data = std::vector<uint8_t>(data.begin(), data.end())});
  }

  scheduleDrain();
  return true;
}

// Rings the doorbell unless a drain is already pending; that drain picks up everything queued so far
void CaptivePortal::WebSocketEndpoint::scheduleDrain()
{
  if (!m_drainQueued.exchange(true)) {
    if (httpd_queue_work(m_server, &WebSocketEndpoint::wsDrainWork, this) != ESP_OK) {
      m_drainQueued.store(false);
      // The messages stay queued: the next send, or retryDrain() on the manager's next tick, tries again
      OS_LOGW(TAG, "httpd work queue full, WebSocket send delayed");
    }
  }
}

void CaptivePortal::WebSocketEndpoint::retryDrain()
{
  if (m_server == nullptr || m_drainQueued.load()) {
    return;
  }
  {
    ScopedLock lock__(&m_sendMutex);
    if (m_sendQueue.empty()) {
      return;
    }
  }
  scheduleDrain();
}

// Runs on the httpd task. httpd_stop (in the destructor) joins that task before any member is destroyed, and a drain
// still in httpd's mailbox then never runs, so `this` is valid here.
void CaptivePortal::WebSocketEndpoint::wsDrainWork(void* arg)
{
  static_cast<WebSocketEndpoint*>(arg)->drainSends();
}

void CaptivePortal::WebSocketEndpoint::drainSends()
{
  while (true) {
    std::deque<WsSend> batch;
    {
      ScopedLock lock__(&m_sendMutex);
      batch.swap(m_sendQueue);
    }

    for (auto& send : batch) {
      httpd_ws_frame_t frame = {};
      frame.final            = true;
      frame.type             = send.binary ? HTTPD_WS_TYPE_BINARY : HTTPD_WS_TYPE_TEXT;
      frame.payload          = send.data.data();
      frame.len              = send.data.size();

      if (send.fd >= 0) {
        // The fd may have closed and been reused by a plain HTTP request since the send was queued
        if (httpd_ws_get_fd_info(m_server, send.fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
          sendToWsClient(m_server, send.fd, frame);
        }
        continue;
      }

      size_t count = CONFIG_OPENSHOCK_PORTAL_HTTPD_MAX_SOCKETS;
      int fds[CONFIG_OPENSHOCK_PORTAL_HTTPD_MAX_SOCKETS];
      if (httpd_get_client_list(m_server, &count, fds) == ESP_OK) {
        for (size_t i = 0; i < count; ++i) {
          if (httpd_ws_get_fd_info(m_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            sendToWsClient(m_server, fds[i], frame);
          }
        }
      }
    }

    // Clear the doorbell, then look again: a producer that saw it set before the clear didn't schedule a drain
    m_drainQueued.store(false);
    {
      ScopedLock lock__(&m_sendMutex);
      if (m_sendQueue.empty()) {
        return;
      }
    }
    if (m_drainQueued.exchange(true)) {
      return;  // A producer scheduled a new drain meanwhile
    }
  }
}

bool CaptivePortal::WebSocketEndpoint::sendMessageTXT(uint8_t socketId, std::string_view data)
{
  int fd = fdForId(socketId);
  if (fd < 0) {
    return false;
  }
  return queueSend(fd, false, false, {reinterpret_cast<const uint8_t*>(data.data()), data.size()});
}

bool CaptivePortal::WebSocketEndpoint::sendMessageBIN(uint8_t socketId, std::span<const uint8_t> data)
{
  int fd = fdForId(socketId);
  if (fd < 0) {
    return false;
  }
  return queueSend(fd, false, true, data);
}

bool CaptivePortal::WebSocketEndpoint::broadcastMessageTXT(std::string_view data)
{
  return queueSend(-1, true, false, {reinterpret_cast<const uint8_t*>(data.data()), data.size()});
}

bool CaptivePortal::WebSocketEndpoint::broadcastMessageBIN(std::span<const uint8_t> data)
{
  return queueSend(-1, true, true, data);
}

bool CaptivePortal::WebSocketEndpoint::hasClients()
{
  ScopedLock lock__(&m_clientsMutex);
  for (uint8_t i = 0; i < CONFIG_OPENSHOCK_PORTAL_MAX_CLIENTS; ++i) {
    if (m_clients[i].used) {
      return true;
    }
  }
  return false;
}
