#include "GatewayConnectionManager.h"

#include "VisualStateManager.h"

#include "Config.h"
#include "Constants.h"
#include "GatewayClient.h"
#include "Logging.h"
#include "Time.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include <memory>
#include <unordered_map>

//
//  ######  ########  ######  ##     ## ########  #### ######## ##    ##    ########  ####  ######  ##    ##
// ##    ## ##       ##    ## ##     ## ##     ##  ##     ##     ##  ##     ##     ##  ##  ##    ## ##   ##
// ##       ##       ##       ##     ## ##     ##  ##     ##      ####      ##     ##  ##  ##       ##  ##
//  ######  ######   ##       ##     ## ########   ##     ##       ##       ########   ##   ######  #####
//       ## ##       ##       ##     ## ##   ##    ##     ##       ##       ##   ##    ##        ## ##  ##
// ##    ## ##       ##    ## ##     ## ##    ##   ##     ##       ##       ##    ##   ##  ##    ## ##   ##
//  ######  ########  ######   #######  ##     ## ####    ##       ##       ##     ## ####  ######  ##    ##
//
// TODO: Fix loading CA Certificate bundles, currently fails with "[esp_crt_bundle.c:161] esp_crt_bundle_init(): Unable to allocate memory for bundle"
// This is probably due to the fact that the bundle is too large for the ESP32's heap or the bundle is incorrectly packedy them
//
#warning SSL certificate verification is currently not implemented, by RFC definition this is a security risk, and allows for MITM attacks, but the realistic risk is low

static const char* const TAG             = "GatewayConnectionManager";
static const char* const AUTH_TOKEN_FILE = "/authToken";

constexpr std::uint8_t FLAG_NONE          = 0;
constexpr std::uint8_t FLAG_HAS_IP        = 1 << 0;
constexpr std::uint8_t FLAG_AUTHENTICATED = 1 << 1;

static std::uint8_t s_flags                                 = 0;
static std::unique_ptr<OpenShock::GatewayClient> s_wsClient = nullptr;

void _evGotIPHandler(arduino_event_t* event) {
  s_flags |= FLAG_HAS_IP;
  ESP_LOGD(TAG, "Got IP address");
}

void _evWiFiDisconnectedHandler(arduino_event_t* event) {
  s_flags    = FLAG_NONE;
  s_wsClient = nullptr;
  ESP_LOGD(TAG, "Lost IP address");
  OpenShock::VisualStateManager::SetWebSocketConnected(false);
}

using namespace OpenShock;

bool GatewayConnectionManager::Init() {
  WiFi.onEvent(_evGotIPHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_evGotIPHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_evWiFiDisconnectedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  return true;
}

bool GatewayConnectionManager::IsConnected() {
  if (s_wsClient == nullptr) {
    return false;
  }

  return s_wsClient->state() == GatewayClient::State::Connected;
}

void GetDeviceInfoFromJsonResponse(HTTPClient& client) {
  ArduinoJson::DynamicJsonDocument doc(1024);  // TODO: profile the normal message size and adjust this accordingly
  deserializeJson(doc, client.getString());

  auto data   = doc["data"];
  String id   = data["id"];
  String name = data["name"];

  ESP_LOGD(TAG, "Device ID:   %s", id.c_str());
  ESP_LOGD(TAG, "Device name: %s", name.c_str());

  auto shockers = data["shockers"];
  for (int i = 0; i < shockers.size(); i++) {
    auto shocker              = shockers[i];
    String shockerId          = shocker["id"];
    std::uint16_t shockerRfId = shocker["rfId"];
    std::uint8_t shockerModel = shocker["model"];

    ESP_LOGD(TAG, "Found shocker %s with RF ID %u and model %u", shockerId.c_str(), shockerRfId, shockerModel);
  }
}

bool GatewayConnectionManager::IsPaired() {
  return (s_flags & FLAG_AUTHENTICATED) != 0;
}

// This method is here to heap usage
std::string GetAuthTokenFromJsonResponse(HTTPClient& client) {
  ArduinoJson::DynamicJsonDocument doc(1024);  // TODO: profile the normal message size and adjust this accordingly
  deserializeJson(doc, client.getString());

  String str = doc["data"];

  return std::string(str.c_str(), str.length());
}

AccountLinkResultCode GatewayConnectionManager::Pair(const char* pairCode) {
  if ((s_flags & FLAG_HAS_IP) == 0) {
    return AccountLinkResultCode::NoInternetConnection;
  }
  s_wsClient = nullptr;

  ESP_LOGD(TAG, "Attempting to pair with pair code %s", pairCode);

  HTTPClient client;

  char uri[256];
  sprintf(uri, OPENSHOCK_API_URL("/1/device/pair/%s"), pairCode);

  client.begin(uri);

  int responseCode = client.GET();

  if (responseCode == 404) {
    return AccountLinkResultCode::InvalidCode;
  }
  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while getting auth token: [%d] %s", responseCode, client.getString().c_str());
    return AccountLinkResultCode::InternalError;
  }

  std::string authToken = GetAuthTokenFromJsonResponse(client);

  client.end();

  if (authToken.empty()) {
    ESP_LOGE(TAG, "Received empty auth token");
    return AccountLinkResultCode::InternalError;
  }

  Config::SetBackendAuthToken(authToken);

  s_flags |= FLAG_AUTHENTICATED;
  ESP_LOGD(TAG, "Successfully paired with pair code %u", pairCode);

  return AccountLinkResultCode::Success;
}
void GatewayConnectionManager::UnPair() {
  s_flags &= FLAG_HAS_IP;
  s_wsClient = nullptr;
  Config::ClearBackendAuthToken();
}

bool FetchDeviceInfo(const std::string& authToken) {
  // TODO: this function is very slow, should be optimized!
  if ((s_flags & FLAG_HAS_IP) == 0) {
    return false;
  }

  HTTPClient client;

  client.begin(OPENSHOCK_API_URL("/1/device/self"));

  client.addHeader("DeviceToken", authToken.c_str());

  int responseCode = client.GET();

  if (responseCode == 401) {
    ESP_LOGD(TAG, "Auth token is invalid, clearing it");
    Config::ClearBackendAuthToken();
    return false;
  }

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while verifying auth token: [%d] %s", responseCode, client.getString().c_str());
    return false;
  }

  GetDeviceInfoFromJsonResponse(client);

  client.end();

  s_flags |= FLAG_AUTHENTICATED;

  return true;
}

static std::int64_t _lastConnectionAttempt = 0;
bool ConnectToLCG() {
  // TODO: this function is very slow, should be optimized!
  if (s_wsClient == nullptr) {  // If wsClient is already initialized, we are already paired or connected
    ESP_LOGD(TAG, "wsClient is null");
    return false;
  }

  if (s_wsClient->state() != GatewayClient::State::Disconnected) {
    ESP_LOGD(TAG, "WebSocketClient is not disconnected, waiting...");
    s_wsClient->disconnect();
    return false;
  }

  std::int64_t msNow = OpenShock::millis();
  if (_lastConnectionAttempt != 0 && (msNow - _lastConnectionAttempt) < 20'000) {  // Only try to connect every 20 seconds
    return false;
  }

  _lastConnectionAttempt = msNow;

  if (!Config::HasBackendAuthToken()) {
    ESP_LOGD(TAG, "No auth token, can't connect to LCG");
    return false;
  }

  std::string authToken = Config::GetBackendAuthToken();

  HTTPClient client;

  client.begin(OPENSHOCK_API_URL("/1/device/assignLCG"));

  client.addHeader("DeviceToken", authToken.c_str());

  int responseCode = client.GET();

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while fetching LCG endpoint: [%d] %s", responseCode, client.getString().c_str());
    return false;
  }

  ArduinoJson::DynamicJsonDocument doc(1024);  // TODO: profile the normal message size and adjust this accordingly
  deserializeJson(doc, client.getString());

  auto data           = doc["data"];
  const char* fqdn    = data["fqdn"];
  const char* country = data["country"];

  client.end();

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
    // Can't connect to the API without WiFi or an auth token
    if ((s_flags & FLAG_HAS_IP) == 0 || !Config::HasBackendAuthToken()) {
      return;
    }

    std::string authToken = Config::GetBackendAuthToken();

    // Test if the auth token is valid
    if (!FetchDeviceInfo(authToken)) {
      ESP_LOGD(TAG, "Auth token is invalid, clearing it");
      Config::ClearBackendAuthToken();
      return;
    }

    s_flags |= FLAG_AUTHENTICATED;
    ESP_LOGD(TAG, "Successfully verified auth token");

    s_wsClient = std::make_unique<GatewayClient>(authToken);
  }

  if (s_wsClient->loop()) {
    return;
  }

  if (ConnectToLCG()) {
    ESP_LOGD(TAG, "Successfully connected to LCG");
    OpenShock::VisualStateManager::SetWebSocketConnected(true);
    return;
  }
}
