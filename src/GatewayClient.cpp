#include <freertos/FreeRTOS.h>

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
#include "util/FnProxy.h"
#include "util/TaskUtils.h"
#include "VisualStateManager.h"

using namespace OpenShock;

static bool s_bootStatusSent = false;

GatewayClient::GatewayClient(const std::string& authToken)
  : m_webSocket()
  , m_state(GatewayClientState::Disconnected)
  , m_loopTask(nullptr)
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
  if (m_loopTask != nullptr) {
    vTaskDelete(m_loopTask);
  }
  m_webSocket.disconnect();
}

void GatewayClient::connect(const char* lcgFqdn)
{
  if (m_state != GatewayClientState::Disconnected) {
    return;
  }

  if (m_loopTask != nullptr) {
    vTaskDelete(m_loopTask);
  }

  esp_err_t err;
  err = TaskUtils::TaskCreateExpensive(&Util::FnProxy<&GatewayClient::_loop>, "GatewayClientLoop", 8192, this, 1, &m_loopTask); // TODO: Profile stack size
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to create GatewayClient loop task: %s", esp_err_to_name(err));
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

  m_webSocket.beginSSL(lcgFqdn, 443, "/2/ws/hub");
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

void GatewayClient::_loop()
{
  while (m_state != GatewayClientState::Disconnected) {
    m_webSocket.loop();
    vTaskDelay(pdMS_TO_TICKS(10));  // 100 Hz update rate
  }

  m_loopTask = nullptr;  // Clear the task handle
  vTaskDelete(nullptr);  // Delete the task
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

  OpenShock::SemVer version;
  if (!OpenShock::TryParseSemVer(OPENSHOCK_FW_VERSION, version)) {
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
    case WStype_PING:
      OS_LOGD(TAG, "Received ping from API");
      break;
    case WStype_PONG:
      OS_LOGV(TAG, "Received pong from API");
      break;
    case WStype_BIN:
      MessageHandlers::WebSocket::HandleGatewayBinary(payload, length);
      break;
    case WStype_FRAGMENT_BIN_START:
      OS_LOGE(TAG, "Received binary fragment start from API, this is not supported!");
      break;
    default:
      OS_LOGE(TAG, "Received unknown event from API");
      break;
  }
}
