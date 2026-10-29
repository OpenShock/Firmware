#include "GatewayConnectionManager.h"

const char* const TAG = "GatewayConnectionManager";

#include "visual/VisualStateManager.h"

#include "AppHooks.h"
#include "config/Config.h"
#include "events/Events.h"
#include "GatewayClient.h"
#include "http/JsonAPI.h"
#include "Logging.h"
#include "serialization/WSLocal.h"
#include "Temporal.h"

#include "SimpleMutex.h"
#include "util/Backoff.h"
#include "util/RetiredList.h"
#include "wifi/WiFiManager.h"

#include <esp_system.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

const char* const AUTH_TOKEN_FILE = "/authToken";

const uint8_t FLAG_NONE   = 0;
const uint8_t FLAG_HAS_IP = 1 << 0;
const uint8_t FLAG_LINKED = 1 << 1;

const uint8_t LINK_CODE_LENGTH = 6;

static std::atomic<uint8_t> s_flags = 0;

// Retry timing, owned by the main task (Update()). Other tasks ask for a reset through s_retryResetRequested.
static OpenShock::Backoff s_authBackoff(300'000, 300'000);   // After the backend rejects the token: 5 minutes
static OpenShock::Backoff s_connectBackoff(20'000, 20'000);  // Between LCG connection attempts: 20 seconds
static OpenShock::Backoff s_hubInfoBackoff(5'000, 300'000);  // After a failed hub info fetch: 5 s doubling up to 5 min
static std::atomic<bool> s_retryResetRequested = false;
static std::atomic<bool> s_restartRequested    = false;
static OpenShock::SimpleMutex s_clientMutex;
static std::shared_ptr<OpenShock::GatewayClient> s_wsClient = nullptr;
// Clients taken out of service but not yet destroyed. Destruction blocks (websocket close + task stop), so it is
// deferred to the main task (Update()) instead of running under s_clientMutex, on the event loop, or on the websocket
// task (where stopping the client from inside its own task is not allowed).
static OpenShock::RetiredList<OpenShock::GatewayClient> s_retiredClients;

// Bumped under s_clientMutex whenever the client is torn down (token change, unlink, WiFi loss). A token verification
// that started under an older generation is stale and must not create a client or mark the hub linked.
static uint32_t s_clientGeneration = 0;

static std::shared_ptr<OpenShock::GatewayClient> GetClient()
{
  OpenShock::ScopedLock lock__(&s_clientMutex);
  return s_wsClient;
}
static uint32_t GetClientGeneration()
{
  OpenShock::ScopedLock lock__(&s_clientMutex);
  return s_clientGeneration;
}
static void DestroyClient()
{
  std::shared_ptr<OpenShock::GatewayClient> client;
  {
    OpenShock::ScopedLock lock__(&s_clientMutex);
    ++s_clientGeneration;
    client = std::move(s_wsClient);  // Leaves s_wsClient null
  }
  if (client != nullptr) {
    client->retire();  // Outside s_clientMutex: it posts an event
    s_retiredClients.retire(std::move(client));
  }
}

static void handleWiFiStateChanged(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;
  (void)id;

  switch (*static_cast<OpenShockWiFiState*>(data)) {
    case OPENSHOCK_WIFI_STATE_GOT_IP:
      s_flags.fetch_or(FLAG_HAS_IP, std::memory_order_relaxed);
      OS_LOGD(TAG, "Got IP address");
      break;
    case OPENSHOCK_WIFI_STATE_DISCONNECTED:
      // Linked is about the token, not the connection; keep it so the UI doesn't show the hub unlinked on WiFi loss
      s_flags.fetch_and(static_cast<uint8_t>(~FLAG_HAS_IP), std::memory_order_relaxed);
      DestroyClient();
      OS_LOGD(TAG, "Lost IP address");
      break;
    default:
      break;
  }
}

// Runs inside esp_restart(), on the task that called it (main, serial console, OTA; never the websocket task, see
// RequestRestart). Shutdown handlers run newest first, so this one is registered when a client is created (see
// registerShutdownHandler), after WiFiManager::Init() registered esp_wifi_stop(): the gateway websocket closes before
// WiFi stops underneath it.
static void handleShutdown()
{
  auto client = GetClient();
  if (client != nullptr) {
    client->retire();  // Outside s_clientMutex: it posts an event
    client->close();
  }
}

// Not in Init(): that runs before WiFiManager::Init(), and a handler registered before esp_wifi_stop()'s would run
// after it. Re-registering is harmless (ESP_ERR_INVALID_STATE), so this runs for every client.
static void registerShutdownHandler()
{
  esp_err_t err = esp_register_shutdown_handler(handleShutdown);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    OS_LOGE(TAG, "Failed to register shutdown handler: %s", esp_err_to_name(err));
  }
}

using namespace OpenShock;
namespace JsonAPI = OpenShock::Serialization::JsonAPI;

bool GatewayConnectionManager::Init()
{
  esp_err_t err = esp_event_handler_register(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_WIFI_STATE_CHANGED, handleWiFiStateChanged, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register WiFi state event handler: %s", esp_err_to_name(err));
    return false;
  }

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

  OS_LOGD(TAG, "Attempting to link to account using code %.*s", static_cast<int>(linkCode.length()), linkCode.data());

  if (linkCode.length() != LINK_CODE_LENGTH) {
    OS_LOGE(TAG, "Invalid link code length: expected %zu, got %zu", static_cast<size_t>(LINK_CODE_LENGTH), linkCode.length());
    return AccountLinkResultCode::InvalidCodeLength;
  }

  auto response = HTTP::JsonAPI::LinkAccount(linkCode);

  if (response.code == 404) {
    OS_LOGW(TAG, "Account link failed: backend rejected the link code as invalid (404)");
    return AccountLinkResultCode::InvalidCode;
  }

  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Account link request failed: %s (result=%hhu, http=%d)", response.ResultToString(), std::to_underlying(response.result), response.code);

    switch (response.result) {
      case HTTP::RequestResult::RateLimited:
        return AccountLinkResultCode::RateLimited;
      case HTTP::RequestResult::TimedOut:
        return AccountLinkResultCode::RequestTimedOut;
      case HTTP::RequestResult::InvalidURL:
      case HTTP::RequestResult::RequestFailed:
        return AccountLinkResultCode::RequestFailed;
      case HTTP::RequestResult::CodeRejected:
        return AccountLinkResultCode::ServerError;
      case HTTP::RequestResult::ParseFailed:
        return AccountLinkResultCode::InvalidResponse;
      default:  // InternalError (e.g. no backend domain configured, URI truncated), Cancelled, ...
        return AccountLinkResultCode::InternalError;
    }
  }

  if (response.code != 200) {
    OS_LOGE(TAG, "Account link failed: unexpected response code %d from backend", response.code);
    return AccountLinkResultCode::ServerError;
  }

  if (response.data.authToken.empty()) {
    OS_LOGE(TAG, "Account link failed: backend returned an empty auth token");
    return AccountLinkResultCode::InvalidResponse;
  }

  if (!Config::SetBackendAuthToken(std::move(response.data.authToken))) {
    OS_LOGE(TAG, "Account link failed: could not persist auth token to flash");
    return AccountLinkResultCode::ConfigSaveFailed;
  }

  // Only drop the existing connection once the new token is in place; a failed link keeps the current session.
  s_retryResetRequested = true;  // A new token deserves an immediate check, whatever the old one's backoff
  DestroyClient();

  s_flags.fetch_or(FLAG_LINKED, std::memory_order_relaxed);
  OS_LOGD(TAG, "Successfully linked to account");

  return AccountLinkResultCode::Success;
}
void GatewayConnectionManager::UnLink()
{
  // Token first: a verification still in flight then either sees it gone or is invalidated by DestroyClient. The flag
  // last: a verification that passed its generation check before DestroyClient may already have set it.
  Config::ClearBackendAuthToken();
  DestroyClient();
  s_flags.fetch_and(static_cast<uint8_t>(~FLAG_LINKED), std::memory_order_relaxed);
}
bool GatewayConnectionManager::SetAuthToken(std::string authToken)
{
  if (!Config::SetBackendAuthToken(std::move(authToken))) {
    return false;
  }

  // Drop the session using the old token; the main task verifies the new one and reconnects (and broadcasts the link status).
  s_flags.fetch_and(static_cast<uint8_t>(~FLAG_LINKED), std::memory_order_relaxed);
  s_retryResetRequested = true;  // A new token deserves an immediate check
  DestroyClient();

  return true;
}

bool GatewayConnectionManager::SendMessageTXT(std::string_view data)
{
  auto client = GetClient();
  if (client == nullptr) {
    return false;
  }

  return client->sendMessageTXT(data);
}

bool GatewayConnectionManager::SendMessageBIN(std::span<const uint8_t> data)
{
  auto client = GetClient();
  if (client == nullptr) {
    return false;
  }

  return client->sendMessageBIN(data);
}

static bool FetchHubInfo(std::string authToken)
{
  // TODO: this function is very slow, should be optimized!
  if ((s_flags.load(std::memory_order_relaxed) & FLAG_HAS_IP) == 0) {
    return false;
  }

  if (!s_authBackoff.ready(OpenShock::millis())) {
    return false;
  }

  auto response = HTTP::JsonAPI::GetHubInfo(std::move(authToken));

  if (response.code == 401) {
    OS_LOGD(TAG, "Auth token is invalid, waiting 5 minutes before checking again");
    s_authBackoff.backOff(OpenShock::millis());
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
    OS_LOGI(TAG, "  [%s] rf=%u model=%u", shocker.id.c_str(), shocker.rfId, std::to_underlying(shocker.model));
  }

  return true;
}

static bool StartConnectingToLCG()
{
  auto client = GetClient();
  if (client == nullptr) {
    OS_LOGD(TAG, "wsClient is null");
    return false;
  }

  if (client->state() != GatewayClientState::Disconnected) {
    return false;
  }

  int64_t msNow = OpenShock::millis();
  if (!s_authBackoff.ready(msNow) || !s_connectBackoff.ready(msNow)) {
    return false;
  }
  s_connectBackoff.backOff(msNow);

  if (!Config::HasBackendAuthToken()) {
    OS_LOGD(TAG, "No auth token, can't connect to LCG");
    return false;
  }

  auto authToken = Config::GetBackendAuthToken();
  if (!authToken) {
    OS_LOGE(TAG, "Failed to get auth token");
    return false;
  }

  auto response = HTTP::JsonAPI::AssignLcg(*authToken);

  if (response.code == 401) {
    OS_LOGD(TAG, "Auth token is invalid, waiting 5 minutes before retrying");
    s_authBackoff.backOff(OpenShock::millis());
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

static void InitializeClient()
{
  DestroyClient();
  uint32_t generation = GetClientGeneration();

  // No client; check prerequisites
  if ((s_flags.load(std::memory_order_relaxed) & FLAG_HAS_IP) == 0 || !Config::HasBackendAuthToken()) {
    return;
  }

  int64_t now = OpenShock::millis();
  if (!s_authBackoff.ready(now) || !s_hubInfoBackoff.ready(now)) {
    return;  // Checked here so an auth hold-off doesn't also count as a hub info failure and stack the two backoffs
  }

  auto authToken = Config::GetBackendAuthToken();
  if (!authToken) {
    OS_LOGE(TAG, "Failed to get auth token");
    return;
  }

  if (!FetchHubInfo(*authToken)) {
    // Back off on failure so an unreachable backend isn't polled on every update tick.
    s_hubInfoBackoff.backOff(OpenShock::millis());
    return;
  }
  s_hubInfoBackoff.reset();

  registerShutdownHandler();

  {
    // The token may have been cleared or replaced (UnLink / Link / SetAuthToken) while the request was in flight.
    // Those tear the client down, bumping the generation, so checking it under the same lock as creating the client
    // leaves no window for them.
    ScopedLock lock__(&s_clientMutex);
    if (s_clientGeneration != generation) {
      OS_LOGD(TAG, "Auth token changed while verifying it, discarding result");
      return;
    }

    s_flags.fetch_or(FLAG_LINKED, std::memory_order_relaxed);
    s_wsClient = std::make_shared<OpenShock::GatewayClient>(*authToken);
  }
  OS_LOGD(TAG, "Successfully verified auth token");

  Serialization::Local::SerializeAccountLinkStatusEvent(true, AppHooks::CaptivePortalBroadcastMessageBIN);
}

// The WiFi state events are edges posted to the default loop with a short timeout, so one can be dropped when its
// queue is full. Reconcile with the actual WiFi state every tick so a lost edge can't strand the connection.
static void reconcileConnectivity()
{
  bool hasIp = (s_flags.load(std::memory_order_relaxed) & FLAG_HAS_IP) != 0;
  if (hasIp && !WiFiManager::IsConnected()) {
    OS_LOGW(TAG, "Missed a WiFi disconnect, dropping the gateway connection");
    s_flags.fetch_and(static_cast<uint8_t>(~FLAG_HAS_IP), std::memory_order_relaxed);
    DestroyClient();
  } else if (!hasIp) {
    char ip[16];
    if (WiFiManager::GetIPAddress(ip) && strcmp(ip, "0.0.0.0") != 0) {
      OS_LOGW(TAG, "Missed a got-IP event, recovering");
      s_flags.fetch_or(FLAG_HAS_IP, std::memory_order_relaxed);
    }
  }
}

void GatewayConnectionManager::RequestRestart()
{
  OS_LOGI(TAG, "Restart requested by gateway");
  s_restartRequested.store(true);
}

void GatewayConnectionManager::Update()
{
  if (s_restartRequested.load()) {
    OS_LOGI(TAG, "Restarting");
    esp_restart();
  }

  s_retiredClients.reap();
  reconcileConnectivity();

  if (s_retryResetRequested.exchange(false)) {
    s_authBackoff.reset();
    s_hubInfoBackoff.reset();
  }

  auto client = GetClient();
  if (client != nullptr) {
    // Client exists; run its loop and optionally reconnect
    if (client->loop()) {
      return;
    }

    StartConnectingToLCG();
    return;
  }

  // Only ever called from the main task, so initialization can't race with itself.
  InitializeClient();
}
