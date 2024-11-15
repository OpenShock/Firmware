#include "GatewayConnectionManager.h"

const char* const TAG = "GatewayConnectionManager";

#include "VisualStateManager.h"

#include "config/Config.h"
#include "GatewayClient.h"
#include "http/JsonAPI.h"
#include "Logging.h"
#include "Time.h"

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

const char* const AUTH_TOKEN_FILE = "/authToken";

const uint8_t FLAG_NONE   = 0;
const uint8_t FLAG_HAS_IP = 1 << 0;
const uint8_t FLAG_LINKED = 1 << 1;

const uint8_t LINK_CODE_LENGTH = 6;

static uint8_t s_flags                                      = 0;
static std::unique_ptr<OpenShock::GatewayClient> s_wsClient = nullptr;

void _evGotIPHandler(arduino_event_t* event)
{
  (void)event;

  s_flags |= FLAG_HAS_IP;
  OS_LOGD(TAG, "Got IP address");
}

void _evWiFiDisconnectedHandler(arduino_event_t* event)
{
  (void)event;

  s_flags    = FLAG_NONE;
  s_wsClient = nullptr;
  OS_LOGD(TAG, "Lost IP address");
}

using namespace OpenShock;
namespace JsonAPI = OpenShock::Serialization::JsonAPI;

bool GatewayConnectionManager::Init()
{
  WiFi.onEvent(_evGotIPHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_evGotIPHandler, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_evWiFiDisconnectedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  return true;
}

bool GatewayConnectionManager::IsConnected()
{
  if (s_wsClient == nullptr) {
    return false;
  }

  return s_wsClient->state() == GatewayClientState::Connected;
}

bool GatewayConnectionManager::IsLinked()
{
  return (s_flags & FLAG_LINKED) != 0;
}

AccountLinkResultCode GatewayConnectionManager::Link(std::string_view linkCode)
{
  if ((s_flags & FLAG_HAS_IP) == 0) {
    return AccountLinkResultCode::NoInternetConnection;
  }
  s_wsClient = nullptr;

  OS_LOGD(TAG, "Attempting to link to account using code %.*s", linkCode.length(), linkCode.data());

  if (linkCode.length() != LINK_CODE_LENGTH) {
    OS_LOGE(TAG, "Invalid link code length");
    return AccountLinkResultCode::InvalidCode;
  }

  auto response = HTTP::JsonAPI::LinkAccount(linkCode);

  if (response.result == HTTP::RequestResult::RateLimited) {
    OS_LOGW(TAG, "Account Link request got ratelimited");
    return AccountLinkResultCode::RateLimited;
  }
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Error while getting auth token: %d %d", response.result, response.code);

    return AccountLinkResultCode::InternalError;
  }

  if (response.code == 404) {
    return AccountLinkResultCode::InvalidCode;
  }

  if (response.code != 200) {
    OS_LOGE(TAG, "Unexpected response code: %d", response.code);
    return AccountLinkResultCode::InternalError;
  }

  std::string_view authToken = response.data.authToken;

  if (authToken.empty()) {
    OS_LOGE(TAG, "Received empty auth token");
    return AccountLinkResultCode::InternalError;
  }

  if (!Config::SetBackendAuthToken(authToken)) {
    OS_LOGE(TAG, "Failed to save auth token");
    return AccountLinkResultCode::InternalError;
  }

  s_flags |= FLAG_LINKED;
  OS_LOGD(TAG, "Successfully linked to account");

  return AccountLinkResultCode::Success;
}
void GatewayConnectionManager::UnLink()
{
  s_flags &= FLAG_HAS_IP;
  s_wsClient = nullptr;
  Config::ClearBackendAuthToken();
}

bool GatewayConnectionManager::SendMessageTXT(std::string_view data)
{
  if (s_wsClient == nullptr) {
    return false;
  }

  return s_wsClient->sendMessageTXT(data);
}

bool GatewayConnectionManager::SendMessageBIN(const uint8_t* data, std::size_t length)
{
  if (s_wsClient == nullptr) {
    return false;
  }

  return s_wsClient->sendMessageBIN(data, length);
}

bool FetchDeviceInfo(std::string_view authToken)
{
  // TODO: this function is very slow, should be optimized!
  if ((s_flags & FLAG_HAS_IP) == 0) {
    return false;
  }

  auto response = HTTP::JsonAPI::GetDeviceInfo(authToken);

  if (response.result == HTTP::RequestResult::RateLimited) {
    return false;  // Just return false, don't spam the console with errors
  }
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Error while fetching device info: %d %d", response.result, response.code);
    return false;
  }

  if (response.code == 401) {
    OS_LOGD(TAG, "Auth token is invalid, clearing it");
    Config::ClearBackendAuthToken();
    return false;
  }

  if (response.code != 200) {
    OS_LOGE(TAG, "Unexpected response code: %d", response.code);
    return false;
  }

  OS_LOGI(TAG, "Device ID:   %s", response.data.deviceId.c_str());
  OS_LOGI(TAG, "Device Name: %s", response.data.deviceName.c_str());
  OS_LOGI(TAG, "Shockers:");
  for (auto& shocker : response.data.shockers) {
    OS_LOGI(TAG, "  [%s] rf=%u model=%u", shocker.id.c_str(), shocker.rfId, shocker.model);
  }

  s_flags |= FLAG_LINKED;

  return true;
}

static int64_t _lastConnectionAttempt = 0;
bool StartConnectingToLCG()
{
  // TODO: this function is very slow, should be optimized!
  if (s_wsClient == nullptr) {  // If wsClient is already initialized, we are already paired or connected
    OS_LOGD(TAG, "wsClient is null");
    return false;
  }

  if (s_wsClient->state() != GatewayClientState::Disconnected) {
    OS_LOGD(TAG, "WebSocketClient is not disconnected, waiting...");
    s_wsClient->disconnect();
    return false;
  }

  int64_t msNow = OpenShock::millis();
  if (_lastConnectionAttempt != 0 && (msNow - _lastConnectionAttempt) < 20'000) {  // Only try to connect every 20 seconds
    return false;
  }

  _lastConnectionAttempt = msNow;

  if (Config::HasBackendLCGOverride()) {
    std::string lcgOverride;
    Config::GetBackendLCGOverride(lcgOverride);

    OS_LOGD(TAG, "Connecting to overridden LCG endpoint %s", lcgOverride.c_str());
    s_wsClient->connect(lcgOverride.c_str());
    return true;
  }

  if (!Config::HasBackendAuthToken()) {
    OS_LOGD(TAG, "No auth token, can't connect to LCG");
    return false;
  }

  std::string authToken;
  if (!Config::GetBackendAuthToken(authToken)) {
    OS_LOGE(TAG, "Failed to get auth token");
    return false;
  }

  auto response = HTTP::JsonAPI::AssignLcg(authToken);

  if (response.result == HTTP::RequestResult::RateLimited) {
    return false;  // Just return false, don't spam the console with errors
  }
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Error while fetching LCG endpoint: %d %d", response.result, response.code);
    return false;
  }

  if (response.code == 401) {
    OS_LOGD(TAG, "Auth token is invalid, clearing it");
    Config::ClearBackendAuthToken();
    return false;
  }

  if (response.code != 200) {
    OS_LOGE(TAG, "Unexpected response code: %d", response.code);
    return false;
  }

  OS_LOGD(TAG, "Connecting to LCG endpoint %s in country %s", response.data.fqdn.c_str(), response.data.country.c_str());
  s_wsClient->connect(response.data.fqdn.c_str());

  return true;
}

void GatewayConnectionManager::Update()
{
  if (s_wsClient == nullptr) {
    // Can't connect to the API without WiFi or an auth token
    if ((s_flags & FLAG_HAS_IP) == 0 || !Config::HasBackendAuthToken()) {
      return;
    }

    std::string authToken;
    if (!Config::GetBackendAuthToken(authToken)) {
      OS_LOGE(TAG, "Failed to get auth token");
      return;
    }

    // Fetch device info
    if (!FetchDeviceInfo(authToken.c_str())) {
      return;
    }

    s_flags |= FLAG_LINKED;
    OS_LOGD(TAG, "Successfully verified auth token");

    s_wsClient = std::make_unique<GatewayClient>(authToken);
  }

  if (s_wsClient->loop()) {
    return;
  }

  StartConnectingToLCG();
}
