#include "CaptivePortal.h"

#include "CommandHandler.h"
#include "Config.h"
#include "Constants.h"
#include "fbs/DeviceToLocalMessage_generated.h"
#include "FormatHelpers.h"
#include "GatewayConnectionManager.h"
#include "Mappers/EspWiFiTypesMapper.h"
#include "MessageHandlers/Local.h"
#include "Utils/HexUtils.h"
#include "WiFiManager.h"
#include "WiFiScanManager.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <esp_log.h>

#include <memory>

static const char* TAG = "CaptivePortal";

constexpr std::uint16_t HTTP_PORT               = 80;
constexpr std::uint16_t WEBSOCKET_PORT          = 81;
constexpr std::uint32_t WEBSOCKET_PING_INTERVAL = 10'000;
constexpr std::uint32_t WEBSOCKET_PING_TIMEOUT  = 1000;
constexpr std::uint8_t WEBSOCKET_PING_RETRIES   = 3;

using namespace OpenShock;

struct CaptivePortalInstance {
  CaptivePortalInstance() : webServer(HTTP_PORT), socketServer(WEBSOCKET_PORT, "/ws", "json") {
    socketServer.onEvent(std::bind(&CaptivePortalInstance::handleWebSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    socketServer.begin();
    socketServer.enableHeartbeat(WEBSOCKET_PING_INTERVAL, WEBSOCKET_PING_TIMEOUT, WEBSOCKET_PING_RETRIES);

    webServer.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");
    webServer.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });
    webServer.begin();
  }
  ~CaptivePortalInstance() {
    webServer.end();
    socketServer.close();
  }

  void handleWebSocketClientConnected(std::uint8_t socketId) {
    ESP_LOGD(TAG, "WebSocket client #%u connected from %s", socketId, socketServer.remoteIP(socketId).toString().c_str());

    flatbuffers::FlatBufferBuilder builder(32);
    Serialization::Local::ReadyMessage readyMessage(true);

    auto readyMessageOffset = builder.CreateStruct(readyMessage);

    auto msg = Serialization::Local::CreateDeviceToLocalMessage(builder, Serialization::Local::DeviceToLocalMessagePayload::ReadyMessage, readyMessageOffset.Union());

    builder.Finish(msg);

    auto span = builder.GetBufferSpan();

    CaptivePortal::SendMessageBIN(socketId, span.data(), span.size());
  }
  void handleWebSocketClientDisconnected(std::uint8_t socketId) { ESP_LOGD(TAG, "WebSocket client #%u disconnected", socketId); }
  void handleWebSocketClientError(std::uint8_t socketId, std::uint16_t code, const char* message) { ESP_LOGE(TAG, "WebSocket client #%u error %u: %s", socketId, code, message); }
  void handleWebSocketEvent(std::uint8_t socketId, WStype_t type, std::uint8_t* payload, std::size_t length) {
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

  AsyncWebServer webServer;
  WebSocketsServer socketServer;
  std::uint64_t wifiScanStartedHandlerHandle;
  std::uint64_t wifiScanCompletedHandlerHandle;
  std::uint64_t wifiScanDiscoveryHandlerHandle;
};

static bool s_alwaysEnabled                                 = false;
static bool s_shouldBeRunning                               = true;
static std::unique_ptr<CaptivePortalInstance> s_webServices = nullptr;

bool _startCaptive() {
  if (s_webServices != nullptr) {
    ESP_LOGD(TAG, "Already started");
    return true;
  }

  ESP_LOGI(TAG, "Starting captive portal");

  if (!WiFi.enableAP(true)) {
    ESP_LOGE(TAG, "Failed to enable AP mode");
    return false;
  }

  if (!WiFi.softAP(("OpenShock-" + WiFi.macAddress()).c_str())) {
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

  return true;
}
void _stopCaptive() {
  if (s_webServices == nullptr) {
    ESP_LOGD(TAG, "Already stopped");
    return;
  }

  ESP_LOGI(TAG, "Stopping captive portal");

  s_webServices = nullptr;

  WiFi.softAPdisconnect(true);
}

using namespace OpenShock;

bool CaptivePortal::Init() {
  // Only start captive portal if we're not connected to a gateway, or if captive is set to always be enabled
  GatewayConnectionManager::RegisterConnectedChangedHandler([](bool connected) { s_shouldBeRunning = !connected || s_alwaysEnabled; });

  return true;
}
void CaptivePortal::SetAlwaysEnabled(bool alwaysEnabled) {
  s_alwaysEnabled   = alwaysEnabled;
  s_shouldBeRunning = !GatewayConnectionManager::IsConnected() || alwaysEnabled;
  Config::SetCaptivePortalConfig({
    .alwaysEnabled = alwaysEnabled,
  });
}
bool CaptivePortal::IsAlwaysEnabled() {
  return s_alwaysEnabled;
}

bool CaptivePortal::IsRunning() {
  return s_webServices != nullptr;
}
void CaptivePortal::Update() {
  if (s_webServices == nullptr) {
    if (s_shouldBeRunning) {
      _startCaptive();
    }
    return;
  }

  if (!s_shouldBeRunning) {
    _stopCaptive();
    return;
  }

  s_webServices->socketServer.loop();
}

bool CaptivePortal::SendMessageTXT(std::uint8_t socketId, const char* data, std::size_t len) {
  if (s_webServices == nullptr) {
    return false;
  }

  s_webServices->socketServer.sendTXT(socketId, data, len);

  return true;
}
bool CaptivePortal::SendMessageBIN(std::uint8_t socketId, const std::uint8_t* data, std::size_t len) {
  if (s_webServices == nullptr) {
    return false;
  }

  s_webServices->socketServer.sendBIN(socketId, data, len);

  return true;
}

bool CaptivePortal::BroadcastMessageTXT(const char* data, std::size_t len) {
  if (s_webServices == nullptr) {
    return false;
  }

  s_webServices->socketServer.broadcastTXT(data, len);

  return true;
}
bool CaptivePortal::BroadcastMessageBIN(const std::uint8_t* data, std::size_t len) {
  if (s_webServices == nullptr) {
    return false;
  }

  s_webServices->socketServer.broadcastBIN(data, len);

  return true;
}
