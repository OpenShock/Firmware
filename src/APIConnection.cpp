#include "APIConnection.h"

#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Constants.h"
#include "ShockerCommandType.h"
#include "Time.h"

#include "fbs/ServerMessages_generated.h"

#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <WString.h>

const char* const TAG = "APIConnection";

using namespace OpenShock;

constexpr ShockerCommandType commandTypeFromFlatbuffers(Serialization::ShockerCommandType commandType) {
  switch (commandType) {
    case Serialization::ShockerCommandType_Stop:
      return ShockerCommandType::Stop;
    case Serialization::ShockerCommandType_Shock:
      return ShockerCommandType::Shock;
    case Serialization::ShockerCommandType_Vibrate:
      return ShockerCommandType::Vibrate;
    case Serialization::ShockerCommandType_Sound:
      return ShockerCommandType::Sound;
    default:
      // This should never happen, but if it does, we'll just stop the device
      return ShockerCommandType::Stop;
  }
}

void handleControlCommandMessage(const Serialization::ShockerCommandList* shockerCommandList) {
  for (const auto& command : *shockerCommandList->commands()) {
    if (!CommandHandler::HandleCommand(command->id(),
                                       commandTypeFromFlatbuffers(command->type()),
                                       command->intensity(),
                                       command->duration(),
                                       command->model()))
    {
      ESP_LOGE(TAG, "Remote command failed/rejected!");
    }
  }
}

void HandleCaptivePortalMessage(const Serialization::CaptivePortalConfig* captivePortalConfig) {
  if (captivePortalConfig->enabled()) {
    CaptivePortal::Start();
  } else {
    CaptivePortal::Stop();
  }
}

using namespace OpenShock;

APIConnection::APIConnection(const String& authToken) : m_webSocket(new WebSocketsClient()) {
  String firmwareVersionHeader = "FirmwareVersion: " + String(Constants::Version);
  String deviceTokenHeader     = "DeviceToken: " + authToken;
  m_webSocket->setExtraHeaders((firmwareVersionHeader + "\"" + deviceTokenHeader).c_str());
  m_webSocket->onEvent(
    std::bind(&APIConnection::handleEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  m_webSocket->beginSSL(Constants::ApiDomain, 443, "/1/ws/device");
}

APIConnection::~APIConnection() {
  m_webSocket->disconnect();
  delete m_webSocket;
}

void APIConnection::Update() {
  std::uint64_t msNow = OpenShock::Millis();

  static std::uint64_t lastKA = 0;
  if ((msNow - lastKA) >= 30'000) {
    sendKeepAlive();
    lastKA = msNow;
  }

  m_webSocket->loop();
}

void APIConnection::parseMessage(const std::uint8_t* data, std::size_t length) {
  flatbuffers::Verifier::Options verifierOptions {
    .max_depth                = 16,
    .max_tables               = 16,
    .check_alignment          = true,
    .check_nested_flatbuffers = true,
    .max_size                 = length,
  };
  flatbuffers::Verifier verifier(data, length, verifierOptions);

  if (!Serialization::VerifyServerMessageBuffer(verifier)) {
    ESP_LOGE(TAG, "Received corrupt/malformed/malicious message from API");
    return;
  }

  auto serverMessage = Serialization::GetServerMessage(data);
  switch (serverMessage->payload_type()) {
    case Serialization::ServerMessagePayload_ShockerCommandList:
      handleControlCommandMessage(serverMessage->payload_as_ShockerCommandList());
      break;
    case Serialization::ServerMessagePayload_CaptivePortalConfig:
      HandleCaptivePortalMessage(serverMessage->payload_as_CaptivePortalConfig());
      break;
    default:
      ESP_LOGE(TAG, "Received unknown message type from API");
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
      ESP_LOGE(TAG, "Received text from API, this is not supported!");
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
      parseMessage(payload, length);
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
