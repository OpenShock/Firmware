#include "APIConnection.h"

#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Constants.h"

#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WString.h>

const char* const TAG = "APIConnection";

using namespace ShockLink;

void handleControlCommandMessage(const DynamicJsonDocument& doc) {
  JsonArrayConst data = doc["Data"];
  for (int i = 0; i < data.size(); i++) {
    JsonObjectConst cur    = data[i];
    std::uint16_t id       = static_cast<std::uint16_t>(cur["Id"]);
    std::uint8_t type      = static_cast<std::uint8_t>(cur["Type"]);
    std::uint8_t intensity = static_cast<std::uint8_t>(cur["Intensity"]);
    unsigned int duration  = static_cast<unsigned int>(cur["Duration"]);
    std::uint8_t model     = static_cast<std::uint8_t>(cur["Model"]);

    if (!CommandHandler::HandleCommand(id, type, intensity, duration, model)) {
      ESP_LOGE(TAG, "Remote command failed/rejected!");
    }
  }
}

void HandleCaptivePortalMessage(const DynamicJsonDocument& doc) {
  bool data = (bool)doc["Data"];

  ESP_LOGD(TAG, "Captive portal debug: %s", data ? "true" : "false");
  if (data) {
    ShockLink::CaptivePortal::Start();
  } else {
    ShockLink::CaptivePortal::Stop();
  }
}

APIConnection::APIConnection(const String& authToken) : m_webSocket(new WebSocketsClient()) {
  String firmwareVersionHeader = "FirmwareVersion: " + String(ShockLink::Constants::Version);
  String deviceTokenHeader     = "DeviceToken: " + authToken;
  m_webSocket->setExtraHeaders((firmwareVersionHeader + "\"" + deviceTokenHeader).c_str());
  m_webSocket->onEvent(
    std::bind(&APIConnection::handleEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  m_webSocket->beginSSL(ShockLink::Constants::ApiDomain, 443, "/1/ws/device");
}

APIConnection::~APIConnection() {
  m_webSocket->disconnect();
  delete m_webSocket;
}

void APIConnection::Update() {
  std::uint64_t msNow = ShockLink::Millis();

  static std::uint64_t lastKA = 0;
  if ((msNow - lastKA) >= 30'000) {
    sendKeepAlive();
    lastKA = msNow;
  }

  m_webSocket->loop();
}

void APIConnection::parseMessage(char* data, std::size_t length) {
  ESP_LOGD(TAG, "Parsing message of length %d", length);
  DynamicJsonDocument doc(1024);  // TODO: profile the normal message size and adjust this accordingly
  deserializeJson(doc, data, length);
  int type = doc["ResponseType"];

  switch (type) {
    case 0:
      handleControlCommandMessage(doc);
      break;
    case 1:
      HandleCaptivePortalMessage(doc);
      break;
  }
}

void APIConnection::handleEvent(WStype_t type, std::uint8_t* payload, std::size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      ESP_LOGI(TAG, "Disconnected from API");
      break;
    case WStype_CONNECTED:
      ESP_LOGI(TAG, "Connected to API");
      sendKeepAlive();
      break;
    case WStype_TEXT:
      parseMessage(reinterpret_cast<char*>(payload), length);
      break;
    case WStype_ERROR:
      ESP_LOGI(TAG, "Received error from API");
      break;
    case WStype_FRAGMENT_TEXT_START:
      ESP_LOGI(TAG, "Received fragment text start from API");
      break;
    case WStype_FRAGMENT:
      ESP_LOGI(TAG, "Received fragment from API");
      break;
    case WStype_FRAGMENT_FIN:
      ESP_LOGI(TAG, "Received fragment fin from API");
      break;
    case WStype_PING:
      ESP_LOGI(TAG, "Received ping from API");
      break;
    case WStype_PONG:
      ESP_LOGI(TAG, "Received pong from API");
      break;
    case WStype_BIN:
      ESP_LOGE(TAG, "Received binary from API, this is not supported!");
      break;
    case WStype_FRAGMENT_BIN_START:
      ESP_LOGE(TAG, "Received binary fragment start from API, this is not supported!");
      break;
    default:
      ESP_LOGE(TAG, "Received unknown event from API");
      break;
  }
}

void APIConnection::sendKeepAlive() {
  if (!m_webSocket->isConnected()) return;

  ESP_LOGD(TAG, "Sending keep alive online state");
  m_webSocket->sendTXT("{\"requestType\": 0}");
}
