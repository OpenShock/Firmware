#include <freertos/FreeRTOS.h>

#include "GatewayClient.h"

const char* const TAG = "GatewayClient";

#include "AppHooks.h"
#include "config/Config.h"
#include "events/Events.h"
#include "Logging.h"
#include "message_handlers/WebSocket.h"
#include "OpenShock.h"
#include "serialization/WSGateway.h"
#include "Temporal.h"
#include "visual/VisualStateManager.h"

#include <esp_crt_bundle.h>

#include <cstring>
#include <utility>

using namespace OpenShock;

const int64_t GATEWAY_PING_TIMEOUT = 90'000;

// One bound for a send: waiting for the handle lock behind another sender and the IDF send itself. Without the bound on
// the lock, a sender queued behind a full-length send could wait longer than the timeout it was promised.
const TickType_t GATEWAY_SEND_TIMEOUT = pdMS_TO_TICKS(10'000);

// Message handlers (config reads, flash writes, FlatBuffers verification) run on the websocket task.
const int GATEWAY_TASK_STACK_SIZE = 6 * 1024;

// Upper bound for a reassembled message: the gateway message handler limit.
const std::size_t GATEWAY_MAX_MESSAGE_SIZE = OpenShock::MessageHandlers::WebSocket::MaxGatewayMessageSize;

// WebSocket opcodes (RFC 6455) as delivered by esp_websocket_client event data.
static constexpr int WS_OP_TEXT   = 0x01;
static constexpr int WS_OP_BINARY = 0x02;

// Set from the websocket task; a retired client's task can still be running when the new client's connects.
static std::atomic<bool> s_bootStatusSent = false;

GatewayClient::GatewayClient(const std::string& authToken)
  : m_headers()
  , m_handleMutex()
  , m_client(nullptr)
  , m_state(GatewayClientState::Disconnected)
  , m_lastPingTimestamp(0)
  , m_retired(false)
  , m_binReasm()
  , m_binReasmActive(false)
  , m_binReasmDiscard(false)
{
  OS_LOGD(TAG, "Creating GatewayClient");

  m_headers = std::string("Firmware-Version: ") + OpenShock::Constants::FW_VERSION + "\r\nDevice-Token: " + authToken + "\r\n";
}
GatewayClient::~GatewayClient()
{
  OS_LOGD(TAG, "Destroying GatewayClient");

  _setState(GatewayClientState::Disconnected);

  // Taken out under the lock so no sender can pick it up again; closed and destroyed outside it (see _destroyHandle).
  esp_websocket_client_handle_t handle = _takeHandle();
  if (handle != nullptr) {
    esp_websocket_client_close(handle, pdMS_TO_TICKS(1000));
    esp_websocket_client_destroy(handle);
  }
}

esp_websocket_client_handle_t GatewayClient::_takeHandle()
{
  // Waits for an in-flight send to finish (bounded by its timeout), then unpublishes the handle so later senders bail.
  ScopedLock lock__(&m_handleMutex);
  return std::exchange(m_client, nullptr);
}

void GatewayClient::_destroyHandle()
{
  esp_websocket_client_handle_t handle = _takeHandle();
  if (handle == nullptr) {
    return;
  }

  // Stops the client task if still running and frees the handle; no further events reach `this` afterwards. Must run
  // without m_handleMutex held: the websocket task may itself be waiting for that mutex in a send (pong, boot status),
  // and destroy waits for that task to exit.
  esp_websocket_client_destroy(handle);
}

void GatewayClient::connect(const std::string& host, uint16_t port, const std::string& path)
{
  if (m_state != GatewayClientState::Disconnected) {
    return;
  }

  // A previous connection's task exits on disconnect but its handle stays allocated; free it before reconnecting.
  _destroyHandle();

  _setState(GatewayClientState::Connecting);

  esp_websocket_client_config_t config = {};
  config.host                          = host.c_str();
  config.port                          = port;
  config.path                          = path.c_str();
  config.transport                     = WEBSOCKET_TRANSPORT_OVER_SSL;
  config.user_agent                    = OpenShock::Constants::UserAgent();
  config.headers                       = m_headers.c_str();
  config.disable_auto_reconnect        = true;                   // GatewayConnectionManager owns reconnection
  config.crt_bundle_attach             = esp_crt_bundle_attach;  // verify server against the compiled-in CA bundle
  config.task_stack                    = GATEWAY_TASK_STACK_SIZE;

  esp_websocket_client_handle_t handle = esp_websocket_client_init(&config);
  if (handle == nullptr) {
    OS_LOGE(TAG, "Failed to initialize WebSocket client");
    _setState(GatewayClientState::Disconnected);
    return;
  }

  esp_websocket_register_events(handle, WEBSOCKET_EVENT_ANY, &GatewayClient::_eventHandler, this);

  // Published before start() so the CONNECTED event's boot status message finds it; senders bail on m_state until then.
  {
    ScopedLock lock__(&m_handleMutex);
    m_client = handle;
  }

  esp_err_t err = esp_websocket_client_start(handle);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
    _destroyHandle();
    _setState(GatewayClientState::Disconnected);
    return;
  }
}

bool GatewayClient::sendMessageTXT(std::string_view data)
{
  ScopedLock lock__(&m_handleMutex, GATEWAY_SEND_TIMEOUT);  // Held across the send so the handle can't be destroyed under it
  if (!lock__.isLocked()) {
    OS_LOGW(TAG, "Timed out waiting for the websocket handle, dropping text message");
    return false;
  }
  if (m_state != GatewayClientState::Connected || m_client == nullptr) {
    return false;
  }

  return esp_websocket_client_send_text(m_client, data.data(), data.length(), GATEWAY_SEND_TIMEOUT) >= 0;
}

bool GatewayClient::sendMessageBIN(std::span<const uint8_t> data)
{
  ScopedLock lock__(&m_handleMutex, GATEWAY_SEND_TIMEOUT);  // Held across the send so the handle can't be destroyed under it
  if (!lock__.isLocked()) {
    OS_LOGW(TAG, "Timed out waiting for the websocket handle, dropping binary message");
    return false;
  }
  if (m_state != GatewayClientState::Connected || m_client == nullptr) {
    return false;
  }

  return esp_websocket_client_send_bin(m_client, reinterpret_cast<const char*>(data.data()), data.size(), GATEWAY_SEND_TIMEOUT) >= 0;
}

void GatewayClient::markPingReceived()
{
  m_lastPingTimestamp = OpenShock::millis();
}

bool GatewayClient::loop()
{
  if (m_state == GatewayClientState::Disconnected) {
    return false;
  }

  // esp_websocket_client runs its own task; we only enforce the app-level ping timeout.
  if (m_state != GatewayClientState::Connected) {
    // Still connecting or disconnecting
    return true;
  }

  // Timestamp is seeded on connect, so a connection that never sees a ping also times out.
  if ((OpenShock::millis() - m_lastPingTimestamp.load()) > GATEWAY_PING_TIMEOUT) {
    OS_LOGW(TAG, "No ping received from gateway for %lld ms, forcing reconnect", GATEWAY_PING_TIMEOUT);
    // The link is presumed dead, so skip the close handshake; destroy stops the client task and frees the handle.
    _destroyHandle();
    _setState(GatewayClientState::Disconnected);
    return false;
  }

  return true;
}

void GatewayClient::_setState(GatewayClientState state)
{
  if (m_state.exchange(state) == state) {
    return;
  }

  // Bounded wait: never block the websocket task or the owning task indefinitely on a full event queue.
  esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED, &state, sizeof(state), pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to post gateway client state change: %s", esp_err_to_name(err));
  }
}

void GatewayClient::_sendBootStatus()
{
  if (s_bootStatusSent) return;

  OS_LOGV(TAG, "Sending Gateway boot status message");

  int32_t updateId;
  if (!Config::GetOtaUpdateId(updateId)) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return;
  }

  OpenShock::OtaUpdateStep updateStep;
  if (!Config::GetOtaUpdateStep(updateStep)) {
    OS_LOGE(TAG, "Failed to get OTA firmware boot type");
    return;
  }

  s_bootStatusSent = Serialization::Gateway::SerializeBootStatusMessage(updateId, AppHooks::OtaGetFirmwareBootType(), [this](std::span<const uint8_t> data) { return sendMessageBIN(data); });

  if (s_bootStatusSent && updateStep != OpenShock::OtaUpdateStep::None) {
    if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::None)) {
      OS_LOGE(TAG, "Failed to reset firmware boot type to normal");
    }
  }
}

void GatewayClient::_eventHandler(void* arg, esp_event_base_t /*base*/, int32_t eventId, void* eventData)
{
  auto* self       = static_cast<GatewayClient*>(arg);
  const auto* data = static_cast<esp_websocket_event_data_t*>(eventData);

  switch (eventId) {
    case WEBSOCKET_EVENT_CONNECTED:
      self->m_lastPingTimestamp = OpenShock::millis();
      self->_setState(GatewayClientState::Connected);
      self->_sendBootStatus();
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
      self->_setState(GatewayClientState::Disconnected);
      break;
    case WEBSOCKET_EVENT_ERROR:
      OS_LOGE(TAG, "Received error from API");
      break;
    case WEBSOCKET_EVENT_DATA:
      if (!self->m_retired.load()) {
        self->_handleData(data);
      }
      break;
    default:
      break;
  }
}

void GatewayClient::_handleData(const esp_websocket_event_data_t* data)
{
  if (data == nullptr) {
    return;
  }

  // Control frames (ping/pong/close) and empty frames carry no application payload.
  if (data->op_code == WS_OP_TEXT) {
    OS_LOGW(TAG, "Received text from API, JSON parsing is not supported anymore :D");
    return;
  }
  if (data->op_code != WS_OP_BINARY && data->op_code != 0x00 /* continuation */) {
    return;
  }

  // esp_websocket_client delivers each frame in chunks (payload_offset/data_len of payload_len),
  // and a message may span several frames (continuation opcode, fin on the last one).
  bool frameComplete = static_cast<std::size_t>(data->payload_offset) + data->data_len >= static_cast<std::size_t>(data->payload_len);
  bool messageEnd    = frameComplete && data->fin;

  // Fast path: a complete single-frame message.
  if (data->op_code == WS_OP_BINARY && data->payload_offset == 0 && messageEnd) {
    m_binReasmActive  = false;
    m_binReasmDiscard = false;
    MessageHandlers::WebSocket::HandleGatewayBinary(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data->data_ptr), data->data_len));
    return;
  }

  if (data->op_code == WS_OP_BINARY && data->payload_offset == 0) {
    // Start of a new message
    m_binReasm.clear();
    m_binReasmActive  = true;
    m_binReasmDiscard = false;
  } else if (!m_binReasmActive) {
    OS_LOGW(TAG, "Dropping continuation data without a message start");
    return;
  }

  if (!m_binReasmDiscard) {
    if (m_binReasm.size() + data->data_len > GATEWAY_MAX_MESSAGE_SIZE) {
      OS_LOGE(TAG, "Fragmented message exceeds %zu bytes, dropping it", GATEWAY_MAX_MESSAGE_SIZE);
      m_binReasm.clear();
      m_binReasm.shrink_to_fit();
      m_binReasmDiscard = true;
    } else {
      m_binReasm.insert(m_binReasm.end(), reinterpret_cast<const uint8_t*>(data->data_ptr), reinterpret_cast<const uint8_t*>(data->data_ptr) + data->data_len);
    }
  }

  if (messageEnd) {
    if (!m_binReasmDiscard) {
      MessageHandlers::WebSocket::HandleGatewayBinary(std::span<const uint8_t>(m_binReasm.data(), m_binReasm.size()));
    }
    m_binReasm.clear();
    m_binReasmActive  = false;
    m_binReasmDiscard = false;
  }
}
