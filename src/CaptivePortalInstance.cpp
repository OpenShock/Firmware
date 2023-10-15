#include "CaptivePortalInstance.h"

#include "_fbs/DeviceToLocalMessage_generated.h"
#include "MessageHandlers/Local.h"

#include <LittleFS.h>

#include <esp_log.h>

static const char* TAG = "CaptivePortalInstance";

constexpr std::uint16_t HTTP_PORT               = 80;
constexpr std::uint16_t WEBSOCKET_PORT          = 81;
constexpr std::uint32_t WEBSOCKET_PING_INTERVAL = 10'000;
constexpr std::uint32_t WEBSOCKET_PING_TIMEOUT  = 1000;
constexpr std::uint8_t WEBSOCKET_PING_RETRIES   = 3;

using namespace OpenShock;

CaptivePortalInstance::CaptivePortalInstance() : webServer(HTTP_PORT), socketServer(WEBSOCKET_PORT, "/ws", "json") {
  socketServer.onEvent(std::bind(&CaptivePortalInstance::handleWebSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
  socketServer.begin();
  socketServer.enableHeartbeat(WEBSOCKET_PING_INTERVAL, WEBSOCKET_PING_TIMEOUT, WEBSOCKET_PING_RETRIES);

  webServer.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");
  webServer.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });
  webServer.begin();
}

CaptivePortalInstance::~CaptivePortalInstance() {
  webServer.end();
  socketServer.close();
}

void CaptivePortalInstance::handleWebSocketClientConnected(std::uint8_t socketId) {
  ESP_LOGD(TAG, "WebSocket client #%u connected from %s", socketId, socketServer.remoteIP(socketId).toString().c_str());

  flatbuffers::FlatBufferBuilder builder(32);
  Serialization::Local::ReadyMessage readyMessage(true);

  auto readyMessageOffset = builder.CreateStruct(readyMessage);

  auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::ReadyMessage, readyMessageOffset.Union());

  builder.Finish(msg);

  auto span = builder.GetBufferSpan();

  sendMessageBIN(socketId, span.data(), span.size());
}

void CaptivePortalInstance::handleWebSocketClientDisconnected(std::uint8_t socketId) {
  ESP_LOGD(TAG, "WebSocket client #%u disconnected", socketId);
}

void CaptivePortalInstance::handleWebSocketClientError(std::uint8_t socketId, std::uint16_t code, const char* message) {
  ESP_LOGE(TAG, "WebSocket client #%u error %u: %s", socketId, code, message);
}

void CaptivePortalInstance::handleWebSocketEvent(std::uint8_t socketId, WStype_t type, std::uint8_t* payload, std::size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      handleWebSocketClientConnected(socketId);
      break;
    case WStype_DISCONNECTED:
      handleWebSocketClientDisconnected(socketId);
      break;
    case WStype_BIN:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      MessageHandlers::Local::Handle(socketId, type, payload, length);
      break;
    case WStype_TEXT:
    case WStype_FRAGMENT_TEXT_START:
      ESP_LOGE(TAG, "Message type is not supported");
      break;
    case WStype_PING:
    case WStype_PONG:
      // Do nothing
      break;
    case WStype_ERROR:
      handleWebSocketClientError(socketId, length, reinterpret_cast<char*>(payload));
      break;
    default:
      ESP_LOGE(TAG, "Unknown WebSocket event type: %d", type);
      break;
  }
}
