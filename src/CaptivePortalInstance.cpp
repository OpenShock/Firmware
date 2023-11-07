#include "CaptivePortalInstance.h"

#include "CommandHandler.h"
#include "event_handlers/WebSocket.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "serialization/WSLocal.h"

#include "wifi/WiFiManager.h"

#include "serialization/_fbs/DeviceToLocalMessage_generated.h"

#include <LittleFS.h>
#include <WiFi.h>

static const char* TAG = "CaptivePortalInstance";

constexpr std::uint16_t HTTP_PORT               = 80;
constexpr std::uint16_t WEBSOCKET_PORT          = 81;
constexpr std::uint32_t WEBSOCKET_PING_INTERVAL = 10'000;
constexpr std::uint32_t WEBSOCKET_PING_TIMEOUT  = 1000;
constexpr std::uint8_t WEBSOCKET_PING_RETRIES   = 3;

using namespace OpenShock;

bool TryReadFile(const char* path, char* buffer, std::size_t& bufferSize) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s for reading", path);
    return false;
  }

  std::size_t fileSize = file.size();
  if (fileSize > bufferSize) {
    ESP_LOGE(TAG, "File %s is too large to fit in buffer", path);
    file.close();
    return false;
  }

  file.readBytes(buffer, fileSize);

  bufferSize = fileSize;

  file.close();

  return true;
}

bool TryGetFsHash(char (&buffer)[65]) {
  std::size_t bufferSize = sizeof(buffer);
  if (TryReadFile("/www/hash.sha1", buffer, bufferSize)) {
    buffer[bufferSize] = '\0';
    return true;
  }
  if (TryReadFile("/www/hash.sha256", buffer, bufferSize)) {
    buffer[bufferSize] = '\0';
    return true;
  }
  if (TryReadFile("/www/hash.md5", buffer, bufferSize)) {
    buffer[bufferSize] = '\0';
    return true;
  }
  return false;
}

CaptivePortalInstance::CaptivePortalInstance()
  : m_webServer(HTTP_PORT), m_socketServer(WEBSOCKET_PORT, "/ws", "json"), m_socketDeFragger(std::bind(&CaptivePortalInstance::handleWebSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)) {
  m_socketServer.onEvent(std::bind(&WebSocketDeFragger::handler, &m_socketDeFragger, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
  m_socketServer.begin();
  m_socketServer.enableHeartbeat(WEBSOCKET_PING_INTERVAL, WEBSOCKET_PING_TIMEOUT, WEBSOCKET_PING_RETRIES);

  // Check if the www folder exists and is populated
  char fsHash[65];
  if (LittleFS.exists("/www/index.html.gz") && TryGetFsHash(fsHash)) {
    ESP_LOGI(TAG, "Serving files from LittleFS");
    ESP_LOGI(TAG, "Filesystem hash: %s", fsHash);

    m_webServer.serveStatic("/", LittleFS, "/www/", "max-age=31536000").setDefaultFile("index.html").setSharedEtag(fsHash);

    m_webServer.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });
  } else {
    ESP_LOGE(TAG, "/www/index.html or hash files not found, serving error page");

    m_webServer.onNotFound([](AsyncWebServerRequest* request) {
      request->send(
        200,
        "text/plain",
        "You probably forgot to upload the Filesystem with PlatformIO!\nGo to PlatformIO -> Platform -> Upload Filesystem Image!\nIf this happened with a file we provided or you just need help, come to the Discord!\n\ndiscord.gg/openshock"
      );
    });
  }

  m_webServer.begin();
}

CaptivePortalInstance::~CaptivePortalInstance() {
  m_webServer.end();
  m_socketServer.close();
}

void CaptivePortalInstance::handleWebSocketClientConnected(std::uint8_t socketId) {
  ESP_LOGD(TAG, "WebSocket client #%u connected from %s", socketId, m_socketServer.remoteIP(socketId).toString().c_str());

  WiFiNetwork connectedNetwork;
  WiFiNetwork* connectedNetworkPtr = nullptr;
  if (WiFiManager::GetConnectedNetwork(connectedNetwork)) {
    connectedNetworkPtr = &connectedNetwork;
  }

  Serialization::Local::SerializeReadyMessage(connectedNetworkPtr, GatewayConnectionManager::IsPaired(), CommandHandler::GetRfTxPin(), std::bind(&CaptivePortalInstance::sendMessageBIN, this, socketId, std::placeholders::_1, std::placeholders::_2));
}

void CaptivePortalInstance::handleWebSocketClientDisconnected(std::uint8_t socketId) {
  ESP_LOGD(TAG, "WebSocket client #%u disconnected", socketId);
}

void CaptivePortalInstance::handleWebSocketClientError(std::uint8_t socketId, std::uint16_t code, const char* message) {
  ESP_LOGE(TAG, "WebSocket client #%u error %u: %s", socketId, code, message);
}

void CaptivePortalInstance::handleWebSocketEvent(std::uint8_t socketId, WebSocketMessageType type, const std::uint8_t* payload, std::size_t length) {
  switch (type) {
    case WebSocketMessageType::Connected:
      handleWebSocketClientConnected(socketId);
      break;
    case WebSocketMessageType::Disconnected:
      handleWebSocketClientDisconnected(socketId);
      break;
    case WebSocketMessageType::Text:
      ESP_LOGE(TAG, "Message type is not supported");
      break;
    case WebSocketMessageType::Binary:
      EventHandlers::WebSocket::HandleLocalBinary(socketId, payload, length);
      break;
    case WebSocketMessageType::Error:
      handleWebSocketClientError(socketId, length, reinterpret_cast<const char*>(payload));
      break;
    case WebSocketMessageType::Ping:
    case WebSocketMessageType::Pong:
      // Do nothing
      break;
    default:
      m_socketDeFragger.clear();
      ESP_LOGE(TAG, "Unknown WebSocket event type: %u", type);
      break;
  }
}
