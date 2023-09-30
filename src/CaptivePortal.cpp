#include "CaptivePortal.h"

#include "AuthenticationManager.h"
#include "WiFiManager.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <memory>

static const char* TAG = "CaptivePortal";

constexpr std::uint16_t HTTP_PORT               = 80;
constexpr std::uint16_t WEBSOCKET_PORT          = 81;
constexpr std::uint32_t WEBSOCKET_PING_INTERVAL = 10'000;
constexpr std::uint32_t WEBSOCKET_PING_TIMEOUT  = 1000;
constexpr std::uint8_t WEBSOCKET_PING_RETRIES   = 3;

struct CaptivePortalInstance {
  CaptivePortalInstance() : webServer(HTTP_PORT), socketServer(WEBSOCKET_PORT, "/ws", "json") { }

  AsyncWebServer webServer;
  WebSocketsServer socketServer;
};
std::unique_ptr<CaptivePortalInstance> s_webServices = nullptr;

void handleWebSocketEvent(std::uint8_t socketId, WStype_t type, std::uint8_t* data, std::size_t len);
void handleHttpNotFound(AsyncWebServerRequest* request);

bool ShockLink::CaptivePortal::Start() {
  if (s_webServices != nullptr) {
    ESP_LOGD(TAG, "Already started");
    return true;
  }

  ESP_LOGD(TAG, "Starting");

  if (!WiFi.enableAP(true)) {
    ESP_LOGE(TAG, "Failed to enable AP mode");
    return false;
  }

  if (!WiFi.softAP(("ShockLink-" + WiFi.macAddress()).c_str())) {
    ESP_LOGE(TAG, "Failed to start AP");
    WiFi.enableAP(false);
    return false;
  }

  IPAddress apIP(10, 10, 10, 10);
  if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0))) {
    ESP_LOGE(TAG, "Failed to configure AP");
    WiFi.softAPdisconnect(true);
    return false;
  }

  s_webServices = std::make_unique<CaptivePortalInstance>();

  s_webServices->socketServer.onEvent(handleWebSocketEvent);
  s_webServices->socketServer.begin();
  s_webServices->socketServer.enableHeartbeat(WEBSOCKET_PING_INTERVAL, WEBSOCKET_PING_TIMEOUT, WEBSOCKET_PING_RETRIES);

  s_webServices->webServer.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");
  s_webServices->webServer.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });
  s_webServices->webServer.begin();

  ESP_LOGD(TAG, "Started");

  return true;
}
void ShockLink::CaptivePortal::Stop() {
  if (s_webServices == nullptr) {
    ESP_LOGD(TAG, "Already stopped");
    return;
  }

  ESP_LOGD(TAG, "Stopping");

  s_webServices->webServer.end();
  s_webServices->socketServer.close();

  s_webServices = nullptr;

  WiFi.softAPdisconnect(true);
}
bool ShockLink::CaptivePortal::IsRunning() {
  return s_webServices != nullptr;
}
void ShockLink::CaptivePortal::Update() {
  if (s_webServices == nullptr) {
    return;
  }

  s_webServices->socketServer.loop();
}
bool ShockLink::CaptivePortal::BroadcastMessageTXT(const char* data, std::size_t len) {
  if (s_webServices == nullptr) {
    return false;
  }

  s_webServices->socketServer.broadcastTXT(data, len);

  return true;
}
bool ShockLink::CaptivePortal::BroadcastMessageBIN(const std::uint8_t* data, std::size_t len) {
  if (s_webServices == nullptr) {
    return false;
  }

  s_webServices->socketServer.broadcastBIN(data, len);

  return true;
}

void handleWebSocketClientConnected(std::uint8_t socketId) {
  ESP_LOGD(
    TAG, "WebSocket client #%u connected from %s", socketId, s_webServices->socketServer.remoteIP(socketId).toString().c_str());
}
void handleWebSocketClientDisconnected(std::uint8_t socketId) {
  ESP_LOGD(TAG, "WebSocket client #%u disconnected", socketId);
}
void handleWebSocketClientMessage(std::uint8_t socketId, WStype_t type, std::uint8_t* data, std::size_t len) {
  if (type != WStype_t::WStype_TEXT) {
    ESP_LOGE(TAG, "Message type is not supported");
    return;
  }

  StaticJsonDocument<256> doc;
  auto err = deserializeJson(doc, data, len);
  if (err) {
    ESP_LOGE(TAG, "Failed to deserialize message: %s", err.c_str());
    return;
  }

  String typeStr = doc["type"];
  if (typeStr.length() == 0) {
    ESP_LOGE(TAG, "Message type is missing");
    return;
  }

  if (typeStr == "startScan") {
    ShockLink::WiFiManager::StartScan();
  } /* else if (typeStr == "connect") {
     ShockLink::WiFiManager::Connect(doc["ssid"], doc["password"]);
   } else if (typeStr == "disconnect") {
     ShockLink::WiFiManager::Disconnect();
   } else if (typeStr == "authenticate") {
     ShockLink::AuthenticationManager::Authenticate(doc["code"]);
   } else if (typeStr == "pair") {
     ShockLink::AuthenticationManager::Pair(doc["code"]);
   } else if (typeStr == "unpair") {
     ShockLink::AuthenticationManager::Unpair();
   } else if (typeStr == "setRmtPin") {
     ShockLink::AuthenticationManager::SetRmtPin(doc["pin"]);
   }*/
}
void handleWebSocketClientError(std::uint8_t socketId, std::uint16_t code, const char* message) {
  ESP_LOGE(TAG, "WebSocket client #%u error %u: %s", socketId, code, message);
}
void handleWebSocketEvent(std::uint8_t socketId, WStype_t type, std::uint8_t* data, std::size_t len) {
  switch (type) {
    case WStype_CONNECTED:
      handleWebSocketClientConnected(socketId);
      break;
    case WStype_DISCONNECTED:
      handleWebSocketClientDisconnected(socketId);
      break;
    case WStype_BIN:
    case WStype_TEXT:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      handleWebSocketClientMessage(socketId, type, data, len);
      break;
    case WStype_PING:
    case WStype_PONG:
      // Do nothing
      break;
    case WStype_ERROR:
      handleWebSocketClientError(socketId, len, reinterpret_cast<char*>(data));
      break;
    default:
      ESP_LOGE(TAG, "Unknown WebSocket event type: %d", type);
      break;
  }
}
