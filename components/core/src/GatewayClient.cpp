#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

using namespace OpenShock;

const int64_t GATEWAY_PING_TIMEOUT = 90'000;

// Covers both the handle lock wait and the send itself
const TickType_t GATEWAY_SEND_TIMEOUT = pdMS_TO_TICKS(10'000);

// Message handlers (config reads, flash writes, FlatBuffers verification) run on the websocket task.
const int GATEWAY_TASK_STACK_SIZE = 6 * 1024;

// Upper bound for a reassembled message: the gateway message handler limit.
const std::size_t GATEWAY_MAX_MESSAGE_SIZE = OpenShock::MessageHandlers::WebSocket::MaxGatewayMessageSize;

// WebSocket opcodes (RFC 6455) as delivered by esp_websocket_client event data.
static constexpr int WS_OP_TEXT   = 0x01;
static constexpr int WS_OP_BINARY = 0x02;

// A retired client's websocket task may still be running
static std::atomic<bool> s_bootStatusSent = false;
// Set on the websocket task once the boot status went out; the OTA step is then cleared from loop() on the main task,
// so the flash write doesn't run on the websocket task (which holds the client lock every other sender waits on).
static std::atomic<bool> s_otaStepResetPending                 = false;
static std::atomic<OpenShock::OtaUpdateStep> s_reportedOtaStep = OpenShock::OtaUpdateStep::None;  // The step it reported

GatewayClient::GatewayClient(const std::string& authToken)
  : m_headers()
  , m_handleMutex()
  , m_client(nullptr)
  , m_wsTask(nullptr)
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

  esp_websocket_client_handle_t handle = _takeHandle();
  if (handle != nullptr) {
    // Not esp_websocket_client_close(): it sends the close frame with portMAX_DELAY, so a stalled TLS write would hang
    // this task. Best-effort close frame with a bounded wait, then destroy stops and joins the websocket task.
    if (esp_websocket_client_is_connected(handle)) {
      esp_websocket_client_send_with_opcode(handle, WS_TRANSPORT_OPCODES_CLOSE, nullptr, 0, pdMS_TO_TICKS(1000));
    }
    esp_websocket_client_destroy(handle);
  }
}

void GatewayClient::retire()
{
  if (m_retired.exchange(true)) {
    return;
  }

  // The last state anyone hears from this client. Posted directly: _setState stays quiet once retired, so a retired
  // client's websocket task or destructor can't report a state that belongs to its successor.
  GatewayClientState state = GatewayClientState::Disconnected;
  if (m_state.exchange(state) != state) {
    esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED, &state, sizeof(state), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to post gateway client state change: %s", esp_err_to_name(err));
    }
  }
}

esp_websocket_client_handle_t GatewayClient::_takeHandle()
{
  ScopedLock lock__(&m_handleMutex);
  return m_client.exchange(nullptr);
}

void GatewayClient::_destroyHandle()
{
  esp_websocket_client_handle_t handle = _takeHandle();
  if (handle == nullptr) {
    return;
  }

  // Not under m_handleMutex: destroy joins the websocket task, which may be waiting on it to send
  esp_websocket_client_destroy(handle);
}

void GatewayClient::connect(const std::string& host, uint16_t port, const std::string& path)
{
  // A retired client may still be connected by a caller that fetched it before it was retired (the LCG lookup is slow)
  if (m_state != GatewayClientState::Disconnected || m_retired.load()) {
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
  // TLS work stays on core 0 with the WiFi/lwIP tasks, away from the E-Stop and RF tasks on core 1
  config.task_core_id_set = true;
  config.task_core_id     = 0;

  esp_websocket_client_handle_t handle = esp_websocket_client_init(&config);
  if (handle == nullptr) {
    OS_LOGE(TAG, "Failed to initialize WebSocket client");
    _setState(GatewayClientState::Disconnected);
    return;
  }

  esp_websocket_register_events(handle, WEBSOCKET_EVENT_ANY, &GatewayClient::_eventHandler, this);

  // Published before start() so the CONNECTED handler can send
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
  return _send(WS_TRANSPORT_OPCODES_TEXT, reinterpret_cast<const uint8_t*>(data.data()), data.length());
}

bool GatewayClient::sendMessageBIN(std::span<const uint8_t> data)
{
  return _send(WS_TRANSPORT_OPCODES_BINARY, data.data(), data.size());
}

bool GatewayClient::_send(ws_transport_opcodes_t opcode, const uint8_t* data, std::size_t len)
{
  // The websocket task runs our callbacks while holding the client's internal lock, so taking m_handleMutex there
  // would invert the lock order against other senders. It needs no lock: destroy joins that task, so the handle
  // outlives the callback.
  if (xTaskGetCurrentTaskHandle() == m_wsTask.load()) {
    esp_websocket_client_handle_t client = m_client.load();
    if (m_state != GatewayClientState::Connected || client == nullptr) {
      return false;
    }
    return esp_websocket_client_send_with_opcode(client, opcode, data, len, GATEWAY_SEND_TIMEOUT) >= 0;
  }

  const TickType_t started = xTaskGetTickCount();
  ScopedTryLock lock__(&m_handleMutex, GATEWAY_SEND_TIMEOUT);
  if (!lock__.isLocked()) {
    OS_LOGW(TAG, "Timed out waiting for the websocket handle, dropping message");
    return false;
  }
  esp_websocket_client_handle_t client = m_client.load();
  if (m_state != GatewayClientState::Connected || client == nullptr) {
    return false;
  }

  const TickType_t elapsed = xTaskGetTickCount() - started;
  if (elapsed >= GATEWAY_SEND_TIMEOUT) {
    return false;
  }

  return esp_websocket_client_send_with_opcode(client, opcode, data, len, GATEWAY_SEND_TIMEOUT - elapsed) >= 0;
}

void GatewayClient::markPingReceived()
{
  m_lastPingTimestamp = OpenShock::millis();
}

bool GatewayClient::loop()
{
  if (s_otaStepResetPending.exchange(false)) {
    // Only the step that was reported: an OTA update started since then owns the step now
    auto reported = s_reportedOtaStep.load();
    if (!Config::CompareExchangeOtaUpdateStep(reported, OpenShock::OtaUpdateStep::None)) {
      OS_LOGW(TAG, "OTA step not reset: it changed since the boot status was sent, or saving failed");
    }
  }

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
  if (m_state.exchange(state) == state || m_retired.load()) {
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
  // A retired client's connection is about to go away; leave the boot status for its successor
  if (s_bootStatusSent || m_retired.load()) return;

  OS_LOGV(TAG, "Sending Gateway boot status message");

  auto updateId = Config::GetOtaUpdateId();
  if (!updateId) {
    OS_LOGE(TAG, "Failed to get OTA update ID");
    return;
  }

  auto updateStep = Config::GetOtaUpdateStep();
  if (!updateStep) {
    OS_LOGE(TAG, "Failed to get OTA firmware boot type");
    return;
  }

  s_bootStatusSent = Serialization::Gateway::SerializeBootStatusMessage(*updateId, AppHooks::OtaGetFirmwareBootType(), [this](std::span<const uint8_t> data) { return sendMessageBIN(data); });

  if (s_bootStatusSent && *updateStep != OpenShock::OtaUpdateStep::None) {
    s_reportedOtaStep     = *updateStep;
    s_otaStepResetPending = true;
  }
}

void GatewayClient::_eventHandler(void* arg, esp_event_base_t /*base*/, int32_t eventId, void* eventData)
{
  auto* self       = static_cast<GatewayClient*>(arg);
  const auto* data = static_cast<esp_websocket_event_data_t*>(eventData);

  // Only BEGIN and FINISH are guaranteed to run on the websocket task (a failing send dispatches ERROR/DISCONNECTED
  // on the sender). Forget the task on FINISH, its last event, so a task reusing the handle isn't trusted.
  if (eventId == WEBSOCKET_EVENT_BEGIN) {
    self->m_wsTask = xTaskGetCurrentTaskHandle();
  } else if (eventId == WEBSOCKET_EVENT_FINISH) {
    self->m_wsTask = nullptr;
  }

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
