#include "GatewayConnectionManager.h"

#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Constants.h"
#include "ShockerCommandType.h"
#include "Time.h"
#include "Utils/FileUtils.h"

#include <esp_log.h>

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <WiFiClientSecure.h>

#include <unordered_map>

static const char* const TAG             = "GatewayConnectionManager";
static const char* const AUTH_TOKEN_FILE = "/authToken";

extern const std::uint8_t* const rootca_crt_bundle_start asm("_binary_data_cert_x509_crt_bundle_start");

static bool s_isPaired                = false;
static bool s_wifiConnected           = false;
static String s_authToken             = "";
static WebSocketsClient* s_webSocket  = nullptr;
static WiFiClientSecure* s_wifiClient = nullptr;
static std::unordered_map<std::uint64_t, OpenShock::GatewayConnectionManager::ConnectedChangedHandler> s_connectedChangedHandlers;

void _sendKeepAlive() {
  if (s_webSocket == nullptr) return;

  if (s_webSocket->isConnected()) {
    ESP_LOGD(TAG, "Sending keep alive online state");
    s_webSocket->sendTXT("{\"requestType\": 0}");
  }
}

void _handleControlCommandMessage(const DynamicJsonDocument& doc) {
  JsonArrayConst data = doc["Data"];
  for (int i = 0; i < data.size(); i++) {
    JsonObjectConst cur    = data[i];
    std::uint16_t id       = static_cast<std::uint16_t>(cur["Id"]);
    std::uint8_t type      = static_cast<std::uint8_t>(cur["Type"]);
    std::uint8_t intensity = static_cast<std::uint8_t>(cur["Intensity"]);
    unsigned int duration  = static_cast<unsigned int>(cur["Duration"]);
    std::uint8_t model     = static_cast<std::uint8_t>(cur["Model"]);

    OpenShock::ShockerCommandType cmdType = static_cast<OpenShock::ShockerCommandType>(type);

    if (!OpenShock::CommandHandler::HandleCommand(id, cmdType, intensity, duration, model)) {
      ESP_LOGE(TAG, "Remote command failed/rejected!");
    }
  }
}

void _handleCaptivePortalMessage(const DynamicJsonDocument& doc) {
  bool data = (bool)doc["Data"];

  ESP_LOGD(TAG, "Captive portal debug: %s", data ? "true" : "false");
  OpenShock::CaptivePortal::SetAlwaysEnabled(data);
}

void _parseMessage(char* data, std::size_t length) {
  ESP_LOGD(TAG, "Parsing message of length %d", length);
  DynamicJsonDocument doc(1024);  // TODO: profile the normal message size and adjust this accordingly
  deserializeJson(doc, data, length);
  int type = doc["ResponseType"];

  switch (type) {
    case 0:
      _handleControlCommandMessage(doc);
      break;
    case 1:
      _handleCaptivePortalMessage(doc);
      break;
  }
}

void _handleEvent(WStype_t type, std::uint8_t* payload, std::size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      ESP_LOGI(TAG, "Disconnected from API");
      for (auto& handler : s_connectedChangedHandlers) {
        handler.second(false);
      }
      break;
    case WStype_CONNECTED:
      ESP_LOGI(TAG, "Connected to API");
      for (auto& handler : s_connectedChangedHandlers) {
        handler.second(true);
      }
      _sendKeepAlive();
      break;
    case WStype_TEXT:
      _parseMessage(reinterpret_cast<char*>(payload), length);
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

void _connect() {
  if (s_webSocket != nullptr) return;

  s_webSocket                  = new WebSocketsClient();
  String firmwareVersionHeader = "FirmwareVersion: " + String(OpenShock::Constants::Version);
  String deviceTokenHeader     = "DeviceToken: " + s_authToken;
  s_webSocket->setExtraHeaders((firmwareVersionHeader + "\"" + deviceTokenHeader).c_str());
  s_webSocket->onEvent(_handleEvent);
  s_webSocket->beginSSL(OpenShock::Constants::ApiDomain, 443, "/1/ws/device");
}

void _disconnect() {
  if (s_webSocket == nullptr) return;

  s_webSocket->disconnect();
  delete s_webSocket;
  s_webSocket = nullptr;
}

void _evWiFiConnectedHandler(arduino_event_t* event) {
  s_wifiConnected = true;
}

void _evWiFiDisconnectedHandler(arduino_event_t* event) {
  s_wifiConnected = false;
  _disconnect();
}

using namespace OpenShock;

bool GatewayConnectionManager::Init() {
  ESP_LOGD(TAG, "Free heap: %f (%f KB)", (1.f - static_cast<float>(ESP.getFreeHeap()) / static_cast<float>(ESP.getHeapSize())) * 100.f, static_cast<float>(ESP.getFreeHeap()) / 1024.f);
  s_wifiClient = new WiFiClientSecure();
  s_wifiClient->setCACertBundle(rootca_crt_bundle_start);
  ESP_LOGD(TAG, "Free heap: %f (%f KB)", (1.f - static_cast<float>(ESP.getFreeHeap()) / static_cast<float>(ESP.getHeapSize())) * 100.f, static_cast<float>(ESP.getFreeHeap()) / 1024.f);
  WiFi.onEvent(_evWiFiConnectedHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_evWiFiConnectedHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_evWiFiDisconnectedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  ESP_LOGD(TAG, "Free heap: %f", static_cast<float>(ESP.getFreeHeap()) / static_cast<float>(ESP.getHeapSize()));

  if (!FileUtils::TryReadFile(AUTH_TOKEN_FILE, s_authToken)) {
    s_authToken = "";
  }
  ESP_LOGD(TAG, "Free heap: %f", static_cast<float>(ESP.getFreeHeap()) / static_cast<float>(ESP.getHeapSize()));

  return true;
}

bool GatewayConnectionManager::IsConnected() {
  if (s_webSocket == nullptr) {
    return false;
  }

  return s_webSocket->isConnected();
}

bool GatewayConnectionManager::IsPaired() {
  if (!s_wifiConnected) {
    return false;
  }
  if (s_isPaired) {
    return true;
  }

  if (!FileUtils::TryReadFile(AUTH_TOKEN_FILE, s_authToken)) {
    return false;
  }

  HTTPClient http;
  const char* const uri = OPENSHOCK_API_URL("/1/device/self");

  ESP_LOGD(TAG, "Contacting self url: %s", uri);
  http.begin(*s_wifiClient, uri);

  int responseCode = http.GET();

  if (responseCode == 401) {
    ESP_LOGD(TAG, "Auth token is invalid, deleting file");
    FileUtils::DeleteFile(AUTH_TOKEN_FILE);
    return false;
  }
  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while verifying auth token: [%d] %s", responseCode, http.getString().c_str());
    return false;
  }

  http.end();

  s_isPaired = true;

  ESP_LOGD(TAG, "Successfully verified auth token");

  return true;
}

bool GatewayConnectionManager::Pair(unsigned int pairCode) {
  if (!s_wifiConnected || s_wifiClient == nullptr) {
    return false;
  }
  _disconnect();

  HTTPClient http;
  String uri = OPENSHOCK_API_URL("/1/device/pair/") + String(pairCode);

  ESP_LOGD(TAG, "Contacting pair code url: %s", uri.c_str());
  http.begin(*s_wifiClient, uri);

  int responseCode = http.GET();

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while getting auth token: [%d] %s", responseCode, http.getString().c_str());
    return false;
  }

  s_authToken = http.getString();

  if (!FileUtils::TryWriteFile(AUTH_TOKEN_FILE, s_authToken)) {
    ESP_LOGE(TAG, "Error while writing auth token to file");

    s_isPaired = false;
    return false;
  }

  http.end();

  s_isPaired = true;

  return true;
}

void GatewayConnectionManager::UnPair() {
  s_isPaired  = false;
  s_authToken = "";

  FileUtils::DeleteFile(AUTH_TOKEN_FILE);
}

std::uint64_t GatewayConnectionManager::RegisterConnectedChangedHandler(ConnectedChangedHandler handler) {
  static std::uint64_t nextHandleId    = 0;
  std::uint64_t handleId               = nextHandleId++;
  s_connectedChangedHandlers[handleId] = handler;
  return handleId;
}
void GatewayConnectionManager::UnRegisterConnectedChangedHandler(std::uint64_t handlerId) {
  auto it = s_connectedChangedHandlers.find(handlerId);

  if (it != s_connectedChangedHandlers.end()) {
    s_connectedChangedHandlers.erase(it);
  }
}

void GatewayConnectionManager::Update() {
  if (s_webSocket == nullptr) {
    if (s_wifiConnected && s_isPaired) {
      ESP_LOGD(TAG, "Connecting to API");
      _connect();
    }

    return;
  }

  if (!IsConnected()) {
    ESP_LOGD(TAG, "Connecting to API");
    s_webSocket->beginSSL(Constants::ApiDomain, 443, "/1/ws/device");
  }

  std::uint64_t msNow = OpenShock::Millis();

  static std::uint64_t lastKA = 0;
  if ((msNow - lastKA) >= 30'000) {
    _sendKeepAlive();
    lastKA = msNow;
  }

  s_webSocket->loop();
}
