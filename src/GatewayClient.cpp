#include "GatewayClient.h"

const char* const TAG = "GatewayClient";

#include "config/Config.h"
#include "events/Events.h"
#include "Logging.h"
#include "message_handlers/WebSocket.h"
#include "OpenShock.h"
#include "OtaUpdateManager.h"
#include "serialization/WSGateway.h"
#include "Temporal.h"
#include "visual/VisualStateManager.h"

#include <cstring>

using namespace OpenShock;

const int64_t GATEWAY_PING_TIMEOUT = 90'000;

// WebSocket opcodes (RFC 6455) as delivered by esp_websocket_client event data.
static constexpr int WS_OP_TEXT   = 0x01;
static constexpr int WS_OP_BINARY = 0x02;

static bool s_bootStatusSent = false;

GatewayClient::GatewayClient(const std::string& authToken)
  : m_headers()
  , m_client(nullptr)
  , m_state(GatewayClientState::Disconnected)
  , m_lastPingTimestamp(0)
  , m_binReasm()
{
  OS_LOGD(TAG, "Creating GatewayClient");

  m_headers = "Firmware-Version: " OPENSHOCK_FW_VERSION "\r\n"
              "Device-Token: "
            + authToken + "\r\n";
}
GatewayClient::~GatewayClient()
{
  OS_LOGD(TAG, "Destroying GatewayClient");

  _setState(GatewayClientState::Disconnected);

  if (m_client != nullptr) {
    esp_websocket_client_close(m_client, pdMS_TO_TICKS(1000));
    esp_websocket_client_destroy(m_client);
    m_client = nullptr;
  }
}

void GatewayClient::connect(const std::string& host, uint16_t port, const std::string& path)
{
  if (m_state != GatewayClientState::Disconnected) {
    return;
  }

  _setState(GatewayClientState::Connecting);

//
//  ######  ########  ######  ##     ## ########  #### ######## ##    ##    ########  ####  ######  ##    ##
// ##    ## ##       ##    ## ##     ## ##     ##  ##     ##     ##  ##     ##     ##  ##  ##    ## ##   ##
// ##       ##       ##       ##     ## ##     ##  ##     ##      ####      ##     ##  ##  ##       ##  ##
//  ######  ######   ##       ##     ## ########   ##     ##       ##       ########   ##   ######  #####
//       ## ##       ##       ##     ## ##   ##    ##     ##       ##       ##   ##    ##        ## ##  ##
// ##    ## ##       ##    ## ##     ## ##    ##   ##     ##       ##       ##    ##   ##  ##    ## ##   ##
//  ######  ########  ######   #######  ##     ## ####    ##       ##       ##     ## ####  ######  ##    ##
//
// TODO: Implement certificate verification
//
#warning SSL certificate verification is currently not implemented, by RFC definition this is a security risk, and allows for MITM attacks, but the realistic risk is low

  esp_websocket_client_config_t config = {};
  config.host                          = host.c_str();
  config.port                          = port;
  config.path                          = path.c_str();
  config.transport                     = WEBSOCKET_TRANSPORT_OVER_SSL;
  config.user_agent                    = OpenShock::Constants::FW_USERAGENT;
  config.headers                       = m_headers.c_str();
  config.disable_auto_reconnect        = true;  // GatewayConnectionManager owns reconnection
  // No CA cert supplied: esp-tls skips verification (CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY).

  m_client = esp_websocket_client_init(&config);
  if (m_client == nullptr) {
    OS_LOGE(TAG, "Failed to initialize WebSocket client");
    _setState(GatewayClientState::Disconnected);
    return;
  }

  esp_websocket_register_events(m_client, WEBSOCKET_EVENT_ANY, &GatewayClient::_eventHandler, this);

  esp_err_t err = esp_websocket_client_start(m_client);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
    esp_websocket_client_destroy(m_client);
    m_client = nullptr;
    _setState(GatewayClientState::Disconnected);
    return;
  }

  OS_LOGW(TAG, "WEBSOCKET CONNECTION BY RFC DEFINITION IS INSECURE, remote endpoint can not be verified due to lack of CA verification support, theoretically this is a security risk and allows for MITM attacks, but the realistic risk is low");
}

void GatewayClient::disconnect()
{
  if (m_state != GatewayClientState::Connected) {
    return;
  }
  _setState(GatewayClientState::Disconnecting);
  if (m_client != nullptr) {
    esp_websocket_client_close(m_client, pdMS_TO_TICKS(1000));
  }
}

bool GatewayClient::sendMessageTXT(std::string_view data)
{
  if (m_state != GatewayClientState::Connected || m_client == nullptr) {
    return false;
  }

  return esp_websocket_client_send_text(m_client, data.data(), data.length(), pdMS_TO_TICKS(10'000)) >= 0;
}

bool GatewayClient::sendMessageBIN(std::span<const uint8_t> data)
{
  if (m_state != GatewayClientState::Connected || m_client == nullptr) {
    return false;
  }

  return esp_websocket_client_send_bin(m_client, reinterpret_cast<const char*>(data.data()), data.size(), pdMS_TO_TICKS(10'000)) >= 0;
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

  if (m_lastPingTimestamp != 0 && (OpenShock::millis() - m_lastPingTimestamp) > GATEWAY_PING_TIMEOUT) {
    OS_LOGW(TAG, "No ping received from gateway for %lld ms, forcing reconnect", GATEWAY_PING_TIMEOUT);
    if (m_client != nullptr) {
      esp_websocket_client_close(m_client, pdMS_TO_TICKS(1000));
    }
    _setState(GatewayClientState::Disconnected);
    return false;
  }

  return true;
}

void GatewayClient::_setState(GatewayClientState state)
{
  if (m_state == state) {
    return;
  }

  m_state = state;

  ESP_ERROR_CHECK(esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_GATEWAY_CLIENT_STATE_CHANGED, &m_state, sizeof(m_state), portMAX_DELAY));
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

  using namespace std::string_view_literals;

  OpenShock::SemVer version;
  if (!OpenShock::TryParseSemVer(OPENSHOCK_FW_VERSION ""sv, version)) {
    OS_LOGE(TAG, "Failed to parse firmware version");
    return;
  }

  s_bootStatusSent = Serialization::Gateway::SerializeBootStatusMessage(updateId, OtaUpdateManager::GetFirmwareBootType(), [this](std::span<const uint8_t> data) { return sendMessageBIN(data); });

  if (s_bootStatusSent && updateStep != OpenShock::OtaUpdateStep::None) {
    if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::None)) {
      OS_LOGE(TAG, "Failed to reset firmware boot type to normal");
    }
  }
}

void GatewayClient::_eventHandler(void* arg, esp_event_base_t /*base*/, int32_t eventId, void* eventData)
{
  auto* self = static_cast<GatewayClient*>(arg);
  const auto* data = static_cast<esp_websocket_event_data_t*>(eventData);

  switch (eventId) {
    case WEBSOCKET_EVENT_CONNECTED:
      self->m_lastPingTimestamp = 0;
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
      self->_handleData(data);
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

  // esp_websocket_client may deliver a message in chunks (payload_offset/payload_len).
  // Fast path: a complete single-frame message.
  if (data->payload_offset == 0 && data->data_len == data->payload_len) {
    MessageHandlers::WebSocket::HandleGatewayBinary(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(data->data_ptr), data->data_len));
    return;
  }

  // Fragmented / chunked message: accumulate until complete.
  if (data->payload_offset == 0) {
    m_binReasm.clear();
    m_binReasm.reserve(data->payload_len);
  }
  m_binReasm.insert(m_binReasm.end(), reinterpret_cast<const uint8_t*>(data->data_ptr), reinterpret_cast<const uint8_t*>(data->data_ptr) + data->data_len);

  if (m_binReasm.size() >= static_cast<size_t>(data->payload_len)) {
    MessageHandlers::WebSocket::HandleGatewayBinary(std::span<const uint8_t>(m_binReasm.data(), m_binReasm.size()));
    m_binReasm.clear();
  }
}
