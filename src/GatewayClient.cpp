#include "GatewayClient.h"

#include "_fbs/DeviceToServerMessage_generated.h"
#include "MessageHandlers/Server.h"
#include "CertificateUtils.h"
#include "Time.h"

#include <esp_log.h>

const char* const TAG = "GatewayClient";

using namespace OpenShock;

GatewayClient::GatewayClient(const std::string& authToken, const std::string& fwVersionStr) : m_webSocket(), m_lastKeepAlive(0), m_state(State::Disconnected) {
  ESP_LOGD(TAG, "Creating GatewayClient");
  std::string headers;
  headers.reserve(512);

  headers += "Firmware-Version: ";
  headers += fwVersionStr;
  headers += "\r\n";
  headers += "Device-Token: ";
  headers += authToken;

  m_webSocket.setExtraHeaders(headers.c_str());
  m_webSocket.onEvent(std::bind(&GatewayClient::_handleEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}
GatewayClient::~GatewayClient() {
  ESP_LOGD(TAG, "Destroying GatewayClient");
  m_webSocket.disconnect();
}

void GatewayClient::connect(const char* lcgFqdn) {
  if (m_state != State::Disconnected) {
    return;
  }

  m_state = State::Connecting;

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

  m_webSocket.beginSSL(lcgFqdn, 443, "/1/ws/device");
  ESP_LOGW(TAG, "WEBSOCKET CONNECTION BY RFC DEFINITION IS INSECURE, remote endpoint can not be verified due to lack of CA verification support, theoretically this is a security risk and allows for MITM attacks, but the realistic risk is low");
}

void GatewayClient::disconnect() {
  if (m_state != State::Connected) {
    return;
  }
  m_state = State::Disconnecting;
  m_webSocket.disconnect();
}

bool GatewayClient::loop() {
  if (m_state == State::Disconnected) {
    return false;
  }

  m_webSocket.loop();

  // We are still in the process of connecting or disconnecting
  if (m_state != State::Connected) {
    // return true to indicate that we are still busy
    return true;
  }

  std::uint64_t msNow = OpenShock::Millis();

  std::uint64_t timeSinceLastKA = msNow - m_lastKeepAlive;

  if (timeSinceLastKA >= 15'000) {
    _sendKeepAlive();
    m_lastKeepAlive = msNow;
  }

  return true;
}

void GatewayClient::_sendKeepAlive() {
  ESP_LOGV(TAG, "Sending keep alive message");

  OpenShock::Serialization::KeepAlive keepAlive(OpenShock::Millis());

  flatbuffers::FlatBufferBuilder builder(64);

  auto keepAliveOffset = builder.CreateStruct(keepAlive);

  auto msg = OpenShock::Serialization::CreateDeviceToServerMessage(builder, OpenShock::Serialization::DeviceToServerMessagePayload::KeepAlive, keepAliveOffset.Union());

  builder.Finish(msg);

  m_webSocket.sendBIN(builder.GetBufferPointer(), builder.GetSize());
}

void GatewayClient::_handleEvent(WStype_t type, std::uint8_t* payload, std::size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      ESP_LOGI(TAG, "Disconnected from API");
      m_state = State::Disconnected;
      break;
    case WStype_CONNECTED:
      ESP_LOGI(TAG, "Connected to API");
      m_state = State::Connected;
      _sendKeepAlive();
      break;
    case WStype_TEXT:
      ESP_LOGW(TAG, "Received text from API, JSON parsing is not supported anymore :D");
      break;
    case WStype_ERROR:
      ESP_LOGE(TAG, "Received error from API");
      break;
    case WStype_FRAGMENT_TEXT_START:
      ESP_LOGD(TAG, "Received fragment text start from API");
      break;
    case WStype_FRAGMENT:
      ESP_LOGD(TAG, "Received fragment from API");
      break;
    case WStype_FRAGMENT_FIN:
      ESP_LOGD(TAG, "Received fragment fin from API");
      break;
    case WStype_PING:
      ESP_LOGD(TAG, "Received ping from API");
      break;
    case WStype_PONG:
      ESP_LOGD(TAG, "Received pong from API");
      break;
    case WStype_BIN:
      MessageHandlers::Server::HandleBinary(payload, length);
      break;
    case WStype_FRAGMENT_BIN_START:
      ESP_LOGE(TAG, "Received binary fragment start from API, this is not supported!");
      break;
    default:
      ESP_LOGE(TAG, "Received unknown event from API");
      break;
  }
}
