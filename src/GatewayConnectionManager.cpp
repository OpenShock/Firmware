#include "GatewayConnectionManager.h"

const char* const TAG = "GatewayConnectionManager";

#include "VisualStateManager.h"

#include "config/Config.h"
#include "Core.h"
#include "GatewayClient.h"
#include "http/JsonAPI.h"
#include "Logging.h"

#include "SimpleMutex.h"

#include <atomic>
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

const char* const AUTH_TOKEN_FILE = "/authToken";

const uint8_t FLAG_NONE   = 0;
const uint8_t FLAG_HAS_IP = 1 << 0;
const uint8_t FLAG_LINKED = 1 << 1;

const uint8_t LINK_CODE_LENGTH = 6;

static std::atomic<uint8_t> s_flags                 = 0;
static std::atomic<int64_t> s_lastAuthFailure       = 0;
static std::atomic<int64_t> s_lastConnectionAttempt = 0;
static std::atomic_flag s_isInitializing            = false;
static OpenShock::SimpleMutex s_clientMutex;
static std::shared_ptr<OpenShock::GatewayClient> s_wsClient = nullptr;

static std::shared_ptr<OpenShock::GatewayClient> GetClient()
{
  OpenShock::ScopedLock lock__(&s_clientMutex);
  return s_wsClient;
}
static void CreateClient(const std::string& authToken)
{
  OpenShock::ScopedLock lock__(&s_clientMutex);
  s_wsClient = std::make_shared<OpenShock::GatewayClient>(authToken);
}
static void DestroyClient()
{
  OpenShock::ScopedLock lock__(&s_clientMutex);
  s_wsClient = nullptr;
}

static void evh_gotIP(arduino_event_t* event)
{
  (void)event;

  s_flags.fetch_or(FLAG_HAS_IP, std::memory_order_relaxed);
  OS_LOGD(TAG, "Got IP address");
}

static void evh_wiFiDisconnected(arduino_event_t* event)
{
  (void)event;

  s_flags.store(FLAG_NONE, std::memory_order_relaxed);
  DestroyClient();
  OS_LOGD(TAG, "Lost IP address");
}

static bool checkIsDeAuthRateLimited(int64_t millis)
{
  return s_lastAuthFailure != 0 && (millis - s_lastAuthFailure) < 300'000;  // 5 Minutes
}
static bool checkIsConnectionRateLimited(int64_t millis)
{
  return s_lastConnectionAttempt != 0 && (millis - s_lastConnectionAttempt) < 20'000;  // 20 seconds
}

using namespace OpenShock;
namespace JsonAPI = OpenShock::Serialization::JsonAPI;

bool GatewayConnectionManager::Init()
{
  WiFi.onEvent(evh_gotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(evh_gotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(evh_wiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  return true;
}

bool GatewayConnectionManager::IsConnected()
{
  auto client = GetClient();
  if (client == nullptr) {
    return false;
  }

  return client->state() == GatewayClientState::Connected;
}

bool GatewayConnectionManager::IsLinked()
{
  return (s_flags.load(std::memory_order_relaxed) & FLAG_LINKED) != 0;
}

AccountLinkResultCode GatewayConnectionManager::Link(std::string_view linkCode)
{
  if ((s_flags.load(std::memory_order_relaxed) & FLAG_HAS_IP) == 0) {
    return AccountLinkResultCode::NoInternetConnection;
  }

  DestroyClient();

  OS_LOGD(TAG, "Attempting to link to account using code %.*s", linkCode.length(), linkCode.data());

  if (linkCode.length() != LINK_CODE_LENGTH) {
    OS_LOGE(TAG, "Invalid link code length");
    return AccountLinkResultCode::InvalidCode;
  }

  auto response = HTTP::JsonAPI::LinkAccount(linkCode);

  if (response.code == 404) {
    return AccountLinkResultCode::InvalidCode;
  }

  if (response.result == HTTP::RequestResult::RateLimited) {
    OS_LOGW(TAG, "Account Link request got ratelimited");
    return AccountLinkResultCode::RateLimited;
  }
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Error while getting auth token: %s %d", response.ResultToString(), response.code);

    return AccountLinkResultCode::InternalError;
  }

  if (response.code != 200) {
    OS_LOGE(TAG, "Unexpected response code: %d", response.code);
    return AccountLinkResultCode::InternalError;
  }

  if (response.data.authToken.empty()) {
    OS_LOGE(TAG, "Received empty auth token");
    return AccountLinkResultCode::InternalError;
  }

  if (!Config::SetBackendAuthToken(std::move(response.data.authToken))) {
    OS_LOGE(TAG, "Failed to save auth token");
    return AccountLinkResultCode::InternalError;
  }

  s_flags.fetch_or(FLAG_LINKED, std::memory_order_relaxed);
  OS_LOGD(TAG, "Successfully linked to account");

  return AccountLinkResultCode::Success;
}
void GatewayConnectionManager::UnLink()
{
  s_flags.fetch_and(static_cast<uint8_t>(~FLAG_LINKED), std::memory_order_relaxed);
  DestroyClient();
  Config::ClearBackendAuthToken();
}

bool GatewayConnectionManager::SendMessageTXT(std::string_view data)
{
  auto client = GetClient();
  if (client == nullptr) {
    return false;
  }

  return client->sendMessageTXT(data);
}

bool GatewayConnectionManager::SendMessageBIN(tcb::span<const uint8_t> data)
{
  auto client = GetClient();
  if (client == nullptr) {
    return false;
  }

  return client->sendMessageBIN(data);
}

bool FetchHubInfo(std::string authToken)
{
  // TODO: this function is very slow, should be optimized!
  if ((s_flags.load(std::memory_order_relaxed) & FLAG_HAS_IP) == 0) {
    return false;
  }

  if (checkIsDeAuthRateLimited(OpenShock::millis())) {
    return false;
  }

  auto response = HTTP::JsonAPI::GetHubInfo(std::move(authToken));

  if (response.code == 401) {
    OS_LOGD(TAG, "Auth token is invalid, waiting 5 minutes before checking again");
    s_lastAuthFailure = OpenShock::millis();
    return false;
  }

  if (response.result == HTTP::RequestResult::RateLimited) {
    return false;  // Just return false, don't spam the console with errors
  }
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Error while fetching hub info: %s %d", response.ResultToString(), response.code);
    return false;
  }

  if (response.code != 200) {
    OS_LOGE(TAG, "Unexpected response code: %d", response.code);
    return false;
  }

  OS_LOGI(TAG, "Hub ID:   %s", response.data.hubId.c_str());
  OS_LOGI(TAG, "Hub Name: %s", response.data.hubName.c_str());
  OS_LOGI(TAG, "Shockers:");
  for (auto& shocker : response.data.shockers) {
    OS_LOGI(TAG, "  [%s] rf=%u model=%u", shocker.id.c_str(), shocker.rfId, shocker.model);
  }

  s_flags.fetch_or(FLAG_LINKED, std::memory_order_relaxed);

  return true;
}

bool StartConnectingToLCG()
{
  auto client = GetClient();
  if (client == nullptr) {
    OS_LOGD(TAG, "wsClient is null");
    return false;
  }

  if (client->state() != GatewayClientState::Disconnected) {
    OS_LOGD(TAG, "WebSocketClient is not disconnected, waiting...");
    client->disconnect();
    return false;
  }

  int64_t msNow = OpenShock::millis();
  if (checkIsDeAuthRateLimited(msNow) || checkIsConnectionRateLimited(msNow)) {
    return false;
  }
  s_lastConnectionAttempt = msNow;

  if (!Config::HasBackendAuthToken()) {
    OS_LOGD(TAG, "No auth token, can't connect to LCG");
    return false;
  }

  std::string authToken;
  if (!Config::GetBackendAuthToken(authToken)) {
    OS_LOGE(TAG, "Failed to get auth token");
    return false;
  }

  auto response = HTTP::JsonAPI::AssignLcg(std::move(authToken));

  if (response.code == 401) {
    OS_LOGD(TAG, "Auth token is invalid, waiting 5 minutes before retrying");
    s_lastAuthFailure = OpenShock::millis();
    return false;
  }

  if (response.result == HTTP::RequestResult::RateLimited) {
    return false;  // Just return false, don't spam the console with errors
  }
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Error while fetching LCG endpoint: %s %d", response.ResultToString(), response.code);
    return false;
  }

  if (response.code != 200) {
    OS_LOGE(TAG, "Unexpected response code: %d", response.code);
    return false;
  }

  OS_LOGI(TAG, "Connecting to LCG endpoint { host: '%s', port: %hu, path: '%s' } in country %s", response.data.host.c_str(), response.data.port, response.data.path.c_str(), response.data.country.c_str());
  client->connect(response.data.host, response.data.port, response.data.path);

  return true;
}

void InitializeClient()
{
  DestroyClient();

  // No client — check prerequisites
  if ((s_flags.load(std::memory_order_relaxed) & FLAG_HAS_IP) == 0 || !Config::HasBackendAuthToken()) {
    return;
  }

  std::string authToken;
  if (!Config::GetBackendAuthToken(authToken)) {
    OS_LOGE(TAG, "Failed to get auth token");
    return;
  }

  if (!FetchHubInfo(authToken)) {
    return;
  }

  s_flags.fetch_or(FLAG_LINKED, std::memory_order_relaxed);
  OS_LOGD(TAG, "Successfully verified auth token");

  CreateClient(authToken);
}

void GatewayConnectionManager::Update()
{
  auto client = GetClient();
  if (client != nullptr) {
    // Client exists — run its loop and optionally reconnect
    if (client->loop()) {
      return;
    }

    StartConnectingToLCG();
    return;
  }

  if (s_isInitializing.test_and_set()) {
    OS_LOGE(TAG, "Was about to initialize GatewayClient, but encountered race condition, yielding.");
    return;
  }

  InitializeClient();

  s_isInitializing.clear();
}
