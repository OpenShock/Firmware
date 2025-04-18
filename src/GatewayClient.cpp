#include "GatewayClient.h"

const char* const TAG = "GatewayClient";

#include "Common.h"
#include "config/Config.h"
#include "events/Events.h"
#include "Logging.h"
#include "message_handlers/WebSocket.h"
#include "OtaUpdateManager.h"
#include "serialization/WSGateway.h"
#include "Time.h"
#include "util/CertificateUtils.h"
#include "VisualStateManager.h"

#include <charconv>

using namespace OpenShock;

static bool s_bootStatusSent = false;

// TODO: THIS IS HORRIBLE!
static bool parse_supplied_address(std::string_view input, std::string& host, uint16_t& port, std::string& path)
{
  host.clear();
  path.clear();
  port = 0;

  if (input.empty()) return false;  // Input cannot be empty

  // Remove scheme if present (e.g., "http://")
  if (auto scheme_pos = input.find("://"); scheme_pos != std::string_view::npos) {
    input.remove_prefix(scheme_pos + 3);
  }

  // Extract path if present
  if (auto path_pos = input.find('/'); path_pos != std::string_view::npos) {
    path = std::string(input.substr(path_pos));
    path += ("/2/ws/hub" + (path.back() == '/' ? 1 : 0));  // I cant be bothered to do anything else so have a conditional assignment combined with C-style pointer arithmetic :>
    input.remove_suffix(input.size() - path_pos);
  } else {
    path = "/2/ws/hub";
  }

  if (input.empty()) return false;

  std::string_view port_part;

  // Check for IPv6 address (e.g., [::1]:8080)
  if (input.front() == '[') {
    auto closing_bracket = input.find(']');
    if (closing_bracket == std::string_view::npos) return false;  // Malformed IPv6

    host = std::string(input.substr(1, closing_bracket - 1));

    if (closing_bracket + 1 < input.size()) {
      if (input[closing_bracket + 1] != ':') return false;  // Expecting ':' after ']'
      port_part = input.substr(closing_bracket + 2);        // Skip "]:" for port
    }
  } else {
    // IPv4 or hostname
    if (auto colon_pos = input.rfind(':'); colon_pos != std::string_view::npos) {
      host      = std::string(input.substr(0, colon_pos));
      port_part = input.substr(colon_pos + 1);
    } else {
      host = std::string(input);
    }
  }

  // Parse port if present
  if (!port_part.empty()) {
    auto result = std::from_chars(port_part.data(), port_part.data() + port_part.size(), port);
    if (result.ec != std::errc()) return false;  // Failed to parse port
  } else {
    port = 443;
  }

  return true;
}

GatewayClient::GatewayClient(const std::string& authToken)
  : m_webSocket()
  , m_state(GatewayClientState::Disconnected)
{
  OS_LOGD(TAG, "Creating GatewayClient");

  std::string headers = "Firmware-Version: " OPENSHOCK_FW_VERSION "\r\n"
                        "Device-Token: "
                      + authToken;

  m_webSocket.setUserAgent(OpenShock::Constants::FW_USERAGENT);
  m_webSocket.setExtraHeaders(headers.c_str());
  m_webSocket.onEvent(std::bind(&GatewayClient::_handleEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}
GatewayClient::~GatewayClient()
{
  _setState(GatewayClientState::Disconnected);

  OS_LOGD(TAG, "Destroying GatewayClient");
  m_webSocket.disconnect();
}

void GatewayClient::connect(std::string_view lcgAddress)
{
  if (m_state != GatewayClientState::Disconnected) {
    return;
  }

  std::string host;
  uint16_t port;
  std::string path;
  if (!parse_supplied_address(lcgAddress, host, port, path)) {
    // TODO: Log error
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

  m_webSocket.beginSSL(host.c_str(), port, path.c_str());
  OS_LOGW(TAG, "WEBSOCKET CONNECTION BY RFC DEFINITION IS INSECURE, remote endpoint can not be verified due to lack of CA verification support, theoretically this is a security risk and allows for MITM attacks, but the realistic risk is low");
}

void GatewayClient::disconnect()
{
  if (m_state != GatewayClientState::Connected) {
    return;
  }
  _setState(GatewayClientState::Disconnecting);
  m_webSocket.disconnect();
}

bool GatewayClient::sendMessageTXT(std::string_view data)
{
  if (m_state != GatewayClientState::Connected) {
    return false;
  }

  return m_webSocket.sendTXT(data.data(), data.length());
}

bool GatewayClient::sendMessageBIN(const uint8_t* data, std::size_t length)
{
  if (m_state != GatewayClientState::Connected) {
    return false;
  }

  return m_webSocket.sendBIN(data, length);
}

bool GatewayClient::loop()
{
  if (m_state == GatewayClientState::Disconnected) {
    return false;
  }

  m_webSocket.loop();

  // We are still in the process of connecting or disconnecting
  if (m_state != GatewayClientState::Connected) {
    // return true to indicate that we are still busy
    return true;
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

  s_bootStatusSent = Serialization::Gateway::SerializeBootStatusMessage(updateId, OtaUpdateManager::GetFirmwareBootType(), [this](const uint8_t* data, std::size_t len) { return m_webSocket.sendBIN(data, len); });

  if (s_bootStatusSent && updateStep != OpenShock::OtaUpdateStep::None) {
    if (!Config::SetOtaUpdateStep(OpenShock::OtaUpdateStep::None)) {
      OS_LOGE(TAG, "Failed to reset firmware boot type to normal");
    }
  }
}

void GatewayClient::_handleEvent(WStype_t type, uint8_t* payload, std::size_t length)
{
  (void)payload;

  switch (type) {
    case WStype_DISCONNECTED:
      _setState(GatewayClientState::Disconnected);
      break;
    case WStype_CONNECTED:
      _setState(GatewayClientState::Connected);
      _sendBootStatus();
      break;
    case WStype_TEXT:
      OS_LOGW(TAG, "Received text from API, JSON parsing is not supported anymore :D");
      break;
    case WStype_ERROR:
      OS_LOGE(TAG, "Received error from API");
      break;
    case WStype_FRAGMENT_TEXT_START:
      OS_LOGD(TAG, "Received fragment text start from API");
      break;
    case WStype_FRAGMENT:
      OS_LOGD(TAG, "Received fragment from API");
      break;
    case WStype_FRAGMENT_FIN:
      OS_LOGD(TAG, "Received fragment fin from API");
      break;
    case WStype_BIN:
      MessageHandlers::WebSocket::HandleGatewayBinary(payload, length);
      break;
    case WStype_FRAGMENT_BIN_START:
      OS_LOGE(TAG, "Received binary fragment start from API, this is not supported!");
      break;
    case WStype_PING:
    case WStype_PONG:
      break;
    default:
      OS_LOGE(TAG, "Received unknown event from API");
      break;
  }
}
