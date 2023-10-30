#include "GatewayConnectionManager.h"

#include "Config.h"
#include "Constants.h"
#include "GatewayClient.h"
#include "Logging.h"
#include "Time.h"

#include <cJSON.h>

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
  String json = client.getString();
  std::shared_ptr<cJSON> root = std::shared_ptr<cJSON>(cJSON_ParseWithLength(json.c_str(), json.length()), cJSON_Delete);
  if (root == nullptr) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != nullptr)
    {
      ESP_LOGE(TAG, "Error parsing JSON: %s", error_ptr);
    }
    return;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
  if (!cJSON_IsObject(data)) {
    ESP_LOGE(TAG, "Invalid JSON response");
    return;
  }

  const cJSON* deviceId = cJSON_GetObjectItemCaseSensitive(data, "id");
  const cJSON* deviceName = cJSON_GetObjectItemCaseSensitive(data, "name");
  const cJSON* deviceShockers = cJSON_GetObjectItemCaseSensitive(data, "shockers");

  if (!cJSON_IsString(deviceId) || !cJSON_IsString(deviceName) || !cJSON_IsArray(deviceShockers)) {
    ESP_LOGE(TAG, "Invalid JSON response");
    return;
  }

  ESP_LOGD(TAG, "Device ID:   %s", deviceId->valuestring);
  ESP_LOGD(TAG, "Device name: %s", deviceName->valuestring);

  cJSON* shocker = nullptr;
  cJSON_ArrayForEach(shocker, deviceShockers) {
    const cJSON* shockerId = cJSON_GetObjectItemCaseSensitive(shocker, "id");
    const cJSON* shockerRfId = cJSON_GetObjectItemCaseSensitive(shocker, "rfId");
    const cJSON* shockerModel = cJSON_GetObjectItemCaseSensitive(shocker, "model");

    if (!cJSON_IsString(shockerId) || !cJSON_IsNumber(shockerRfId) || !cJSON_IsNumber(shockerModel)) {
      ESP_LOGE(TAG, "Invalid JSON response");
      return;
    }

    const char* shockerIdStr = shockerId->valuestring;
    int shockerRfIdInt = shockerRfId->valueint;
    int shockerModelInt = shockerModel->valueint;

    if (shockerRfIdInt < 0 || shockerRfIdInt > UINT16_MAX || shockerModelInt < 0 || shockerModelInt > UINT8_MAX) {
      ESP_LOGE(TAG, "Invalid JSON response");
      return;
    }

    std::uint16_t shockerRfIdU16 = shockerRfIdInt;
    std::uint8_t shockerModelU8 = shockerModelInt;

    ESP_LOGD(TAG, "Found shocker %s with RF ID %u and model %u", shockerIdStr, shockerRfIdU16, shockerModelU8);
  }
}

bool GatewayConnectionManager::IsPaired() {
  return (s_flags & FLAG_AUTHENTICATED) != 0;
}

std::string GetAuthTokenFromJsonResponse(HTTPClient& client) {
  String json = client.getString();
  std::shared_ptr<cJSON> root = std::shared_ptr<cJSON>(cJSON_ParseWithLength(json.c_str(), json.length()), cJSON_Delete);
  if (root == nullptr) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != nullptr)
    {
      ESP_LOGE(TAG, "Error parsing JSON: %s", error_ptr);
    }
    return "";
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
  if (!cJSON_IsString(data)) {
    ESP_LOGE(TAG, "Invalid JSON response");
    return "";
  }


  return std::string(data->valuestring);
}

GatewayPairResultCode GatewayConnectionManager::Pair(const char* pairCode) {
  if ((s_flags & FLAG_HAS_IP) == 0) {
    return GatewayPairResultCode::NoInternetConnection;
  }
  s_wsClient = nullptr;

  ESP_LOGD(TAG, "Attempting to pair with pair code %s", pairCode);

  HTTPClient client;

  char uri[256];
  sprintf(uri, OPENSHOCK_API_URL("/1/device/pair/%s"), pairCode);

  client.begin(uri);

  int responseCode = client.GET();

  if (responseCode == 404) {
    return GatewayPairResultCode::InvalidCode;
  }
  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while getting auth token: [%d] %s", responseCode, client.getString().c_str());
    return GatewayPairResultCode::InternalError;
  }

  std::string authToken = GetAuthTokenFromJsonResponse(client);

  client.end();

  if (authToken.empty()) {
    ESP_LOGE(TAG, "Received empty auth token");
    return GatewayPairResultCode::InternalError;
  }

  Config::SetBackendAuthToken(authToken);

  s_flags |= FLAG_AUTHENTICATED;
  ESP_LOGD(TAG, "Successfully paired with pair code %s", pairCode);

  return GatewayPairResultCode::Success;
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

  String json = client.getString();

  client.end();

  std::shared_ptr<cJSON> root = std::shared_ptr<cJSON>(cJSON_ParseWithLength(json.c_str(), json.length()), cJSON_Delete);
  if (root == nullptr) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != nullptr)
    {
      ESP_LOGE(TAG, "Error parsing JSON: %s", error_ptr);
    }
    return false;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
  if (!cJSON_IsObject(data)) {
    ESP_LOGE(TAG, "Invalid JSON response");
    return false;
  }

  const cJSON* fqdn    = cJSON_GetObjectItemCaseSensitive(data, "fqdn");
  const cJSON* country = cJSON_GetObjectItemCaseSensitive(data, "country");

  if (!cJSON_IsString(fqdn) || !cJSON_IsString(country)) {
    ESP_LOGE(TAG, "Invalid JSON response");
    return false;
  }

  const char* fqdnStr    = fqdn->valuestring;
  const char* countryStr = country->valuestring;

  ESP_LOGD(TAG, "Connecting to LCG endpoint %s in country %s", fqdnStr, countryStr);
  s_wsClient->connect(fqdnStr);

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

    s_wsClient = std::make_unique<GatewayClient>(authToken, OPENSHOCK_FW_VERSION);
  }

  if (s_wsClient->loop()) {
    return;
  }

  if (ConnectToLCG()) {
    ESP_LOGD(TAG, "Successfully connected to LCG");
    return;
  }
}
