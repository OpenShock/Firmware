#include "CaptivePortal.h"

#include "Config.h"
#include "fbs/DeviceToLocalMessage_generated.h"
#include "fbs/LocalToDeviceMessage_generated.h"
#include "FormatHelpers.h"
#include "GatewayConnectionManager.h"
#include "Mappers/EspWiFiTypesMapper.h"
#include "Utils/HexUtils.h"
#include "WiFiManager.h"
#include "WiFiScanManager.h"

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
  void handleWebSocketClientWiFiScanCommand(const Serialization::Local::WifiScanCommand* msg) {
    if (msg->run()) {
      WiFiScanManager::StartScan();
    } else {
      WiFiScanManager::AbortScan();
    }
  }
  void handleWebSocketClientWiFiNetworkSaveCommand(const Serialization::Local::WifiNetworkSaveCommand* msg) {
    auto ssid     = msg->ssid();
    auto bssid    = msg->bssid();
    auto password = msg->password();

    if (ssid == nullptr || bssid == nullptr || password == nullptr) {
      ESP_LOGE(TAG, "WiFi message is missing required properties");
      return;
    }

    if (ssid->size() > 31) {
      ESP_LOGE(TAG, "WiFi SSID is too long");
      return;
    }

    if (bssid->size() != 17) {
      ESP_LOGE(TAG, "WiFi BSSID is invalid (wrong length)");
      return;
    }

    // Convert BSSID to byte array
    std::uint8_t bssidBytes[6];
    if (!HexUtils::TryParseHexMac<17>(nonstd::span<const char, 17>(bssid->data(), bssid->size()), bssidBytes)) {
      ESP_LOGE(TAG, "WiFi BSSID is invalid (failed to parse)");
      return;
    }

    if (password->size() > 63) {
      ESP_LOGE(TAG, "WiFi password is too long");
      return;
    }

    if (!WiFiManager::Save(bssidBytes, password->str())) {  // TODO: support SSID as well
      ESP_LOGE(TAG, "Failed to save WiFi network");
    }
  }
  void handleWebSocketClientWiFiNetworkForgetCommand(const Serialization::Local::WifiNetworkForgetCommand* msg) {
    auto ssid  = msg->ssid();
    auto bssid = msg->bssid();

    if (ssid == nullptr && bssid == nullptr) {
      ESP_LOGE(TAG, "WiFi message is missing required properties");
      return;
    }

    if (ssid != nullptr && ssid->size() > 31) {
      ESP_LOGE(TAG, "WiFi SSID is too long");
      return;
    }

    if (bssid != nullptr && bssid->size() != 17) {
      ESP_LOGE(TAG, "WiFi BSSID is invalid (wrong length)");
      return;
    }

    // Convert BSSID to byte array
    std::uint8_t bssidBytes[6];
    if (bssid != nullptr && !HexUtils::TryParseHexMac<17>(nonstd::span<const char, 17>(bssid->data(), bssid->size()), bssidBytes)) {
      ESP_LOGE(TAG, "WiFi BSSID is invalid (failed to parse)");
      return;
    }

    if (!WiFiManager::Forget(bssidBytes)) {  // TODO: support SSID as well
      ESP_LOGE(TAG, "Failed to forget WiFi network");
    }
  }
  void handleWebSocketClientWiFiNetworkConnectCommand(const Serialization::Local::WifiNetworkConnectCommand* msg) {
    auto ssid  = msg->ssid();
    auto bssid = msg->bssid();

    if (ssid == nullptr && bssid == nullptr) {
      ESP_LOGE(TAG, "WiFi message is missing required properties");
      return;
    }

    if (ssid != nullptr && ssid->size() > 31) {
      ESP_LOGE(TAG, "WiFi SSID is too long");
      return;
    }

    if (bssid != nullptr && bssid->size() != 17) {
      ESP_LOGE(TAG, "WiFi BSSID is invalid (wrong length)");
      return;
    }

    // Convert BSSID to byte array
    std::uint8_t bssidBytes[6];
    if (bssid != nullptr && !HexUtils::TryParseHexMac<17>(nonstd::span<const char, 17>(bssid->data(), bssid->size()), bssidBytes)) {
      ESP_LOGE(TAG, "WiFi BSSID is invalid (failed to parse)");
      return;
    }

    if (!WiFiManager::Connect(bssidBytes)) {  // TODO: support SSID as well
      ESP_LOGE(TAG, "Failed to connect to WiFi network");
    }
  }
  void handleWebSocketClientWiFiNetworkDisconnectCommand(const Serialization::Local::WifiNetworkDisconnectCommand* msg) { WiFiManager::Disconnect(); }
  void handleWebSocketClientGatewayPairCommand(const Serialization::Local::GatewayPairCommand* msg) {
    auto code = msg->code();

    if (code == nullptr) {
      ESP_LOGE(TAG, "Gateway message is missing required properties");
      return;
    }

    if (code->size() != 6) {
      ESP_LOGE(TAG, "Gateway code is invalid (wrong length)");
      return;
    }

    GatewayConnectionManager::Pair(code->data());
  }
  void handleWebSocketClientGatewayUnPairCommand(const Serialization::Local::GatewayUnpairCommand* msg) { GatewayConnectionManager::UnPair(); }
  void handleWebSocketClientSetRfTxPinCommand(const Serialization::Local::SetRfTxPinCommand* msg) {
    auto pin = msg->pin();

    // AuthenticationManager::SetRmtPin(pin->data());
  }
  void handleWebSocketClientMessage(std::uint8_t socketId, WStype_t type, std::uint8_t* data, std::size_t len) {
    if (type != WStype_t::WStype_BIN) {
      ESP_LOGE(TAG, "Message type is not supported");
      return;
    }

    // Deserialize
    auto msg = flatbuffers::GetRoot<Serialization::Local::LocalToDeviceMessage>(data);
    if (msg == nullptr) {
      ESP_LOGE(TAG, "Failed to deserialize message");
      return;
    }

    // Validate buffer
    flatbuffers::Verifier::Options verifierOptions {
      .max_size = 4096,  // TODO: Profile this
    };
    flatbuffers::Verifier verifier(data, len, verifierOptions);
    if (!msg->Verify(verifier)) {
      ESP_LOGE(TAG, "Failed to verify message");
      return;
    }

    switch (msg->payload_type()) {
      case Serialization::Local::LocalToDeviceMessagePayload::WifiScanCommand:
        handleWebSocketClientWiFiScanCommand(msg->payload_as_WifiScanCommand());
        break;
      case Serialization::Local::LocalToDeviceMessagePayload::WifiNetworkSaveCommand:
        handleWebSocketClientWiFiNetworkSaveCommand(msg->payload_as_WifiNetworkSaveCommand());
        break;
      case Serialization::Local::LocalToDeviceMessagePayload::WifiNetworkForgetCommand:
        handleWebSocketClientWiFiNetworkForgetCommand(msg->payload_as_WifiNetworkForgetCommand());
        break;
      case Serialization::Local::LocalToDeviceMessagePayload::WifiNetworkConnectCommand:
        handleWebSocketClientWiFiNetworkConnectCommand(msg->payload_as_WifiNetworkConnectCommand());
        break;
      case Serialization::Local::LocalToDeviceMessagePayload::WifiNetworkDisconnectCommand:
        handleWebSocketClientWiFiNetworkDisconnectCommand(msg->payload_as_WifiNetworkDisconnectCommand());
        break;
      case Serialization::Local::LocalToDeviceMessagePayload::GatewayPairCommand:
        handleWebSocketClientGatewayPairCommand(msg->payload_as_GatewayPairCommand());
        break;
      case Serialization::Local::LocalToDeviceMessagePayload::GatewayUnpairCommand:
        handleWebSocketClientGatewayUnPairCommand(msg->payload_as_GatewayUnpairCommand());
        break;
      case Serialization::Local::LocalToDeviceMessagePayload::SetRfTxPinCommand:
        handleWebSocketClientSetRfTxPinCommand(msg->payload_as_SetRfTxPinCommand());
        break;
      default:
        ESP_LOGE(TAG, "Received message with unknown payload type");
        return;
    }
  }
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
        handleWebSocketClientMessage(socketId, type, payload, length);
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
