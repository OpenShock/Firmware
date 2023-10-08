#include "GatewayConnectionManager.h"

#include "CaptivePortal.h"
#include "CommandHandler.h"
#include "Config.h"
#include "Constants.h"
#include "ShockerCommandType.h"
#include "Time.h"

#include <esp_log.h>

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <WiFiClientSecure.h>

#include <memory>
#include <unordered_map>

static const char* const TAG             = "GatewayConnectionManager";
static const char* const AUTH_TOKEN_FILE = "/authToken";

extern const std::uint8_t* const rootca_crt_bundle_start asm("_binary_data_cert_x509_crt_bundle_start");

static std::unordered_map<std::uint64_t, OpenShock::GatewayConnectionManager::ConnectedChangedHandler> s_connectedChangedHandlers;

struct GatewayClient {
  GatewayClient(const std::string& authToken, const std::string& fwVersionStr) : m_webSocket() {
    std::string firmwareVersionHeader = "FirmwareVersion: " + fwVersionStr;
    std::string deviceTokenHeader     = "DeviceToken: " + authToken;

    m_webSocket.setExtraHeaders((firmwareVersionHeader + "\"" + deviceTokenHeader).c_str());
    m_webSocket.onEvent(std::bind(&GatewayClient::_handleEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
  }

  enum class State {
    Disconnected,
    Disconnecting,
    Connecting,
    Connected,
  };

  State state() { return m_state; }

  void connect(const char* lcgFqdn) {
    if (m_state != State::Disconnected) {
      return;
    }
    m_state = State::Connecting;
    m_webSocket.beginSSL(lcgFqdn, 443, "/1/ws/device");
  }

  void disconnect() {
    if (m_state != State::Connected) {
      return;
    }
    m_state = State::Disconnecting;
    m_webSocket.disconnect();
  }

  bool loop() {
    if (m_state == State::Disconnected) {
      return false;
    }

    m_webSocket.loop();

    if (m_state != State::Connected) {
      return true;
    }

    std::uint64_t msNow = OpenShock::Millis();

    std::uint64_t timeSinceLastKA = msNow - m_lastKeepAlive;

    if (timeSinceLastKA >= 30'000) {
      _sendKeepAlive();
      m_lastKeepAlive = msNow;
    }

    return true;
  }

  WebSocketsClient m_webSocket;
  std::uint64_t m_lastKeepAlive = 0;
  State m_state                 = State::Disconnected;

private:
  void _sendKeepAlive() {
    if (m_webSocket.isConnected()) {
      ESP_LOGD(TAG, "Sending keep alive online state");
      m_webSocket.sendTXT("{\"requestType\": 0}");
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
        m_state = State::Disconnected;
        for (auto& handler : s_connectedChangedHandlers) {
          handler.second(false);
        }
        break;
      case WStype_CONNECTED:
        ESP_LOGI(TAG, "Connected to API");
        m_state = State::Connected;
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
};

constexpr std::uint8_t FLAG_NONE           = 0;
constexpr std::uint8_t FLAG_WIFI_CONNECTED = 1 << 0;
constexpr std::uint8_t FLAG_DEVICE_PAIRED  = 1 << 1;

static std::uint8_t s_flags = 0;
// static WiFiClientSecure* s_wifiClient            = nullptr;
static std::unique_ptr<GatewayClient> s_wsClient = nullptr;

void _evWiFiConnectedHandler(arduino_event_t* event) {
  s_flags |= FLAG_WIFI_CONNECTED;
}

void _evWiFiDisconnectedHandler(arduino_event_t* event) {
  s_flags    = FLAG_NONE;
  s_wsClient = nullptr;
}

using namespace OpenShock;

bool GatewayConnectionManager::Init() {
  //
  //  ######  ########  ######  ##     ## ########  #### ######## ##    ##    ########  ####  ######  ##    ##
  // ##    ## ##       ##    ## ##     ## ##     ##  ##     ##     ##  ##     ##     ##  ##  ##    ## ##   ##
  // ##       ##       ##       ##     ## ##     ##  ##     ##      ####      ##     ##  ##  ##       ##  ##
  //  ######  ######   ##       ##     ## ########   ##     ##       ##       ########   ##   ######  #####
  //       ## ##       ##       ##     ## ##   ##    ##     ##       ##       ##   ##    ##        ## ##  ##
  // ##    ## ##       ##    ## ##     ## ##    ##   ##     ##       ##       ##    ##   ##  ##    ## ##   ##
  //  ######  ########  ######   #######  ##     ## ####    ##       ##       ##     ## ####  ######  ##    ##
  //
  // WARNING: Skipping SSL Verification!
  //
  // Fix loading CA Certificate bundles, currently fails with "[esp_crt_bundle.c:161] esp_crt_bundle_init(): Unable to allocate memory for bundle"
  // This is probably due to the fact that the bundle is too large for the ESP32's heap or the bundle is incorrectly packed
  //
  //
  // s_wifiClient = new WiFiClientSecure();
  // s_wifiClient->setCACertBundle(rootca_crt_bundle_start);
  //
  //
  //  ######  ########  ######  ##     ## ########  #### ######## ##    ##    ########  ####  ######  ##    ##
  // ##    ## ##       ##    ## ##     ## ##     ##  ##     ##     ##  ##     ##     ##  ##  ##    ## ##   ##
  // ##       ##       ##       ##     ## ##     ##  ##     ##      ####      ##     ##  ##  ##       ##  ##
  //  ######  ######   ##       ##     ## ########   ##     ##       ##       ########   ##   ######  #####
  //       ## ##       ##       ##     ## ##   ##    ##     ##       ##       ##   ##    ##        ## ##  ##
  // ##    ## ##       ##    ## ##     ## ##    ##   ##     ##       ##       ##    ##   ##  ##    ## ##   ##
  //  ######  ########  ######   #######  ##     ## ####    ##       ##       ##     ## ####  ######  ##    ##
  //
  WiFi.onEvent(_evWiFiConnectedHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_evWiFiConnectedHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_evWiFiDisconnectedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  return true;
}

bool GatewayConnectionManager::IsConnected() {
  if (s_wsClient == nullptr) {
    return false;
  }

  return s_wsClient->state() == GatewayClient::State::Connected;
}

bool GatewayConnectionManager::IsPaired() {
  // TODO: this function is very slow, should be optimized!
  if (s_flags & FLAG_DEVICE_PAIRED) {
    return true;
  }
  if ((s_flags & FLAG_WIFI_CONNECTED) == 0) {
    return false;
  }
  if (Config::GetBackendAuthToken().empty()) {
    return false;
  }

  HTTPClient http;
  const char* const uri = OPENSHOCK_API_URL("/1/device/self");

  ESP_LOGD(TAG, "Contacting self url: %s", uri);
  http.begin(uri);  // TODO: http.begin(*s_wifiClient, uri);

  // TODO: Attach auth token

  int responseCode = http.GET();

  if (responseCode == 401) {
    ESP_LOGD(TAG, "Auth token is invalid, clearing it");
    Config::ClearBackendAuthToken();
    return false;
  }
  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while verifying auth token: [%d] %s", responseCode, http.getString().c_str());
    return false;
  }

  http.end();

  s_flags |= FLAG_DEVICE_PAIRED;

  ESP_LOGD(TAG, "Successfully verified auth token");

  return true;
}

// This method is here to heap usage
std::string GetAuthTokenFromJsonResponse(HTTPClient& http) {
  ArduinoJson::DynamicJsonDocument doc(1024);  // TODO: profile the normal message size and adjust this accordingly
  deserializeJson(doc, http.getString());

  String str = doc["data"];

  return std::string(str.c_str(), str.length());
}

bool GatewayConnectionManager::Pair(unsigned int pairCode) {
  if ((s_flags & FLAG_WIFI_CONNECTED) == 0) {
    return false;
  }
  s_wsClient = nullptr;

  ESP_LOGD(TAG, "Attempting to pair with pair code %u", pairCode);

  HTTPClient http;

  char uri[256];
  sprintf(uri, OPENSHOCK_API_URL("/1/device/pair/%u"), pairCode);

  http.begin(uri);  // TODO: http.begin(*s_wifiClient, uri);

  int responseCode = http.GET();

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while getting auth token: [%d] %s", responseCode, http.getString().c_str());
    return false;
  }

  std::string authToken = GetAuthTokenFromJsonResponse(http);

  http.end();

  if (authToken.empty()) {
    ESP_LOGE(TAG, "Received empty auth token");
    return false;
  }

  Config::SetBackendAuthToken(authToken);

  s_wsClient = std::make_unique<GatewayClient>(authToken, OpenShock::Constants::Version);

  return true;
}

void GatewayConnectionManager::UnPair() {
  s_flags &= FLAG_WIFI_CONNECTED;
  s_wsClient = nullptr;
  Config::ClearBackendAuthToken();
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

static std::uint64_t _lastConnectionAttempt = 0;
bool ConnectToLCG() {
  // TODO: this function is very slow, should be optimized!
  if (s_wsClient == nullptr) {  // If wsClient is already initialized, we are already paired or connected
    return false;
  }

  if (s_wsClient->state() != GatewayClient::State::Disconnected) {
    s_wsClient->disconnect();
    return false;
  }

  std::uint64_t msNow = Millis();
  if ((msNow - _lastConnectionAttempt) < 20'000) {  // Only try to connect every 20 seconds
    return false;
  }

  _lastConnectionAttempt = msNow;

  std::string authToken = Config::GetBackendAuthToken();

  if (authToken.empty()) {
    return false;
  }

  ESP_LOGD(TAG, "Attempting to connect to LCG endpoint with auth token %s", authToken.c_str());

  HTTPClient http;

  http.begin(OPENSHOCK_API_URL("/1/device/assignLCG"));  // TODO: http.begin(*s_wifiClient, uri);
  http.addHeader("DeviceToken", authToken.c_str());

  int responseCode = http.GET();

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while fetching LCG endpoint: [%d] %s", responseCode, http.getString().c_str());
    return false;
  }

  ArduinoJson::DynamicJsonDocument doc(1024);  // TODO: profile the normal message size and adjust this accordingly
  deserializeJson(doc, http.getString());

  auto data           = doc["data"];
  const char* fqdn    = data["fqdn"];
  const char* country = data["country"];

  http.end();

  if (fqdn == nullptr || country == nullptr) {
    ESP_LOGE(TAG, "Received invalid response from LCG endpoint");
    return false;
  }

  ESP_LOGD(TAG, "Connecting to LCG endpoint %s in country %s", fqdn, country);
  s_wsClient->connect(fqdn);

  return true;
}

void GatewayConnectionManager::Update() {
  if (s_wsClient == nullptr) {
    return;
  }

  if (s_wsClient->loop()) {
    return;
  }

  if (ConnectToLCG()) {
    return;
  }
}
