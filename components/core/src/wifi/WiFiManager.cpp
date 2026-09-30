#include <freertos/FreeRTOS.h>

#include "wifi/WiFiManager.h"

const char* const TAG = "WiFiManager";

#include "captiveportal/Manager.h"
#include "config/Config.h"
#include "events/Events.h"
#include "FormatHelpers.h"
#include "Logging.h"
#include "serialization/WSLocal.h"
#include "Temporal.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiNetwork.h"
#include "wifi/WiFiScanManager.h"

#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>

#include "SimpleMutex.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace OpenShock;

/// Returns a user-friendly disconnect reason, or nullptr for obscure/protocol-level errors.
static const char* wifiDisconnectReason(uint8_t reason)
{
  switch (reason) {
    // Authentication / password issues
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
      return "Authentication failed, check your password";

    // Network not reachable
    case WIFI_REASON_NO_AP_FOUND:
      return "Network not found, is it in range?";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "Lost connection to the network, out of range?";
    case WIFI_REASON_CONNECTION_FAIL:
      return "Could not connect to the network";

    // AP rejected us
    case WIFI_REASON_ASSOC_TOOMANY:
      return "Network is full, too many devices connected";
    case WIFI_REASON_NOT_ENOUGH_BANDWIDTH:
      return "Network is too busy, try again later";
    case WIFI_REASON_ASSOC_FAIL:
      return "Network rejected the connection";
    case WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION:
      return "Network rejected the connection";

    // Normal disconnects
    case WIFI_REASON_AUTH_LEAVE:
    case WIFI_REASON_ASSOC_LEAVE:
    case WIFI_REASON_STA_LEAVING:
      return "Disconnected from the network";
    case WIFI_REASON_AP_INITIATED:
      return "Disconnected by the network";
    case WIFI_REASON_ROAMING:
      return "Switching access points";

    // Timeouts
    case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:  // formerly WIFI_REASON_ASSOC_EXPIRE (renamed in ESP-IDF 6)
    case WIFI_REASON_TIMEOUT:
    case WIFI_REASON_MISSING_ACKS:
      return "Connection timed out";

    // Everything else is too technical for the user
    default:
      return nullptr;
  }
}

enum class WiFiState : uint8_t {
  Disconnected = 0,
  Connecting   = 1 << 0,
  Connected    = 1 << 1,
};

static esp_netif_t* s_staNetif = nullptr;
static std::atomic<WiFiState> s_wifiState {WiFiState::Disconnected};
static uint8_t s_connectedBSSID[6]                   = {0};
static std::atomic<uint8_t> s_connectedCredentialsID = 0;
static std::atomic<uint8_t> s_preferredCredentialsID = 0;
static OpenShock::SimpleMutex s_networksMutex;
static std::vector<WiFiNetwork> s_wifiNetworks;

// Broadcast a coarse connectivity change on the OpenShock event bus. Posted with a
// short timeout because this runs on the default event loop task (posting to the
// same loop must never block on a full queue).
static void postWiFiState(OpenShockWiFiState state)
{
  esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_WIFI_STATE_CHANGED, &state, sizeof(state), pdMS_TO_TICKS(100));
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to post WiFi state event: %s", esp_err_to_name(err));
  }
}

static bool attractivityComparer(const WiFiNetwork& a, const WiFiNetwork& b)
{
  // Networks with credentials sort before those without
  if (a.credentialsID != 0 && b.credentialsID == 0) return true;
  if (a.credentialsID == 0 && b.credentialsID != 0) return false;

  // Fewer connect attempts is more attractive
  if (a.connectAttempts != b.connectAttempts) return a.connectAttempts < b.connectAttempts;

  // Higher RSSI is more attractive
  return a.rssi > b.rssi;
}
static bool isConnectRateLimited(const WiFiNetwork& net)
{
  if (net.lastConnectAttempt == 0) {
    return false;
  }

  int64_t now  = OpenShock::millis();
  int64_t diff = now - net.lastConnectAttempt;
  if ((net.connectAttempts > 5 && diff < 5000) || (net.connectAttempts > 10 && diff < 10'000) || (net.connectAttempts > 15 && diff < 30'000) || (net.connectAttempts > 20 && diff < 60'000)) {
    return true;
  }

  return false;
}

static bool isSaved(std::function<bool(const Config::WiFiCredentials&)> predicate)
{
  return Config::AnyWiFiCredentials(predicate);
}
static std::vector<WiFiNetwork>::iterator findNetwork(std::function<bool(WiFiNetwork&)> predicate, bool sortByAttractivity = true)
{
  if (sortByAttractivity) {
    std::sort(s_wifiNetworks.begin(), s_wifiNetworks.end(), attractivityComparer);
  }
  return std::find_if(s_wifiNetworks.begin(), s_wifiNetworks.end(), predicate);
}
static std::vector<WiFiNetwork>::iterator findNetworkBySSID(const char* ssid, bool sortByAttractivity = true)
{
  return findNetwork([ssid](const WiFiNetwork& net) noexcept { return strcmp(net.ssid, ssid) == 0; }, sortByAttractivity);
}
static std::vector<WiFiNetwork>::iterator findNetworkByBSSID(const uint8_t (&bssid)[6])
{
  return findNetwork([bssid](const WiFiNetwork& net) noexcept { return memcmp(net.bssid, bssid, sizeof(bssid)) == 0; }, false);
}
static std::vector<WiFiNetwork>::iterator findNetworkByCredentialsID(uint8_t credentialsID, bool sortByAttractivity = true)
{
  return findNetwork([credentialsID](const WiFiNetwork& net) noexcept { return net.credentialsID == credentialsID; }, sortByAttractivity);
}

static bool getNextWiFiNetwork(OpenShock::Config::WiFiCredentials& creds)
{
  return findNetwork([&creds](const WiFiNetwork& net) {
    if (net.credentialsID == 0) {
      return false;
    }

    if (isConnectRateLimited(net)) {
      return false;
    }

    if (!Config::TryGetWiFiCredentialsByID(net.credentialsID, creds)) {
      return false;
    }

    return true;
  }) != s_wifiNetworks.end();
}

static bool connectWiFi(const std::string& ssid, const std::string& password, wifi_auth_mode_t expectedAuthMode = WIFI_AUTH_MAX, const uint8_t* pinnedBssid = nullptr)
{
  if (ssid.empty()) {
    OS_LOGW(TAG, "Cannot connect to network with empty SSID");
    return false;
  }

  if (pinnedBssid != nullptr) {
    OS_LOGV(TAG, "Connecting to network %s (pinned " BSSID_FMT ")", ssid.c_str(), BSSID_ARG(pinnedBssid));
  } else {
    OS_LOGV(TAG, "Connecting to network %s", ssid.c_str());
  }

  // Mark as attempted and validate auth mode if we know the network from scanning
  auto it = findNetworkBySSID(ssid.c_str());
  if (it != s_wifiNetworks.end()) {
    // Reject if the AP's auth mode is weaker than expected (evil twin protection)
    if (expectedAuthMode != WIFI_AUTH_MAX && it->authMode < expectedAuthMode) {
      OS_LOGW(TAG, "Rejecting network %s: auth mode %d is weaker than expected %d", ssid.c_str(), it->authMode, expectedAuthMode);
      return false;
    }
    it->connectAttempts++;
    it->lastConnectAttempt = OpenShock::millis();
  }

  wifi_config_t config = {};
  std::strncpy(reinterpret_cast<char*>(config.sta.ssid), ssid.c_str(), sizeof(config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(config.sta.password), password.c_str(), sizeof(config.sta.password));
  // Let esp_wifi pick the strongest matching BSSID and retry a few times before giving up.
  config.sta.scan_method       = WIFI_ALL_CHANNEL_SCAN;
  config.sta.sort_method       = WIFI_CONNECT_AP_BY_SIGNAL;
  config.sta.failure_retry_cnt = 3;
  if (pinnedBssid != nullptr) {
    config.sta.bssid_set = true;
    std::memcpy(config.sta.bssid, pinnedBssid, sizeof(config.sta.bssid));
  }

  s_wifiState.store(WiFiState::Connecting, std::memory_order_relaxed);
  postWiFiState(OPENSHOCK_WIFI_STATE_CONNECTING);

  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
    s_wifiState.store(WiFiState::Disconnected, std::memory_order_relaxed);
    postWiFiState(OPENSHOCK_WIFI_STATE_DISCONNECTED);
    return false;
  }

  err = esp_wifi_connect();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    s_wifiState.store(WiFiState::Disconnected, std::memory_order_relaxed);
    postWiFiState(OPENSHOCK_WIFI_STATE_DISCONNECTED);
    return false;
  }

  return true;
}

static bool authenticate(const WiFiNetwork& net, std::string_view password)
{
  uint8_t id = Config::AddWiFiCredentials(net.ssid, password, net.authMode);
  if (id == 0) {
    Serialization::Local::SerializeErrorMessage("too_many_credentials", CaptivePortal::BroadcastMessageBIN);
    return false;
  }

  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Saved, net, CaptivePortal::BroadcastMessageBIN);

  return connectWiFi(net.ssid, std::string(password));
}

static void evWiFiConnected(const wifi_event_sta_connected_t& info)
{
  s_wifiState.store(WiFiState::Connected, std::memory_order_relaxed);
  memcpy(s_connectedBSSID, info.bssid, sizeof(s_connectedBSSID));
  postWiFiState(OPENSHOCK_WIFI_STATE_CONNECTED);

  // info.ssid is not guaranteed null-terminated; bound by ssid_len.
  char ssid[33];
  size_t ssidLen = std::min(static_cast<size_t>(info.ssid_len), sizeof(ssid) - 1);
  memcpy(ssid, info.ssid, ssidLen);
  ssid[ssidLen] = '\0';

  ScopedLock lock__(&s_networksMutex);

  auto it = findNetworkByBSSID(info.bssid);
  if (it == s_wifiNetworks.end()) {
    s_connectedCredentialsID.store(0, std::memory_order_relaxed);

    OS_LOGW(TAG, "Connected to unscanned network \"%s\", BSSID: " BSSID_FMT, ssid, BSSID_ARG(info.bssid));

    Config::WiFiCredentials creds;
    if (Config::TryGetWiFiCredentialsBySSID(ssid, creds)) {
      s_connectedCredentialsID.store(creds.id, std::memory_order_relaxed);
    }

    return;
  }

  s_connectedCredentialsID.store(it->credentialsID, std::memory_order_relaxed);

  OS_LOGI(TAG, "Connected to network %s (" BSSID_FMT ")", ssid, BSSID_ARG(info.bssid));

  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Connected, *it, CaptivePortal::BroadcastMessageBIN);
}
static void evWiFiGotIP(const ip_event_got_ip_t& info)
{
  uint8_t ip[4];
  memcpy(ip, &info.ip_info.ip.addr, sizeof(ip));

  OS_LOGI(TAG, "Got IP address " IPV4ADDR_FMT " from network " BSSID_FMT, IPV4ADDR_ARG(ip), BSSID_ARG(s_connectedBSSID));

  postWiFiState(OPENSHOCK_WIFI_STATE_GOT_IP);

  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), IPV4ADDR_FMT, IPV4ADDR_ARG(ip));
  Serialization::Local::SerializeWiFiGotIpEvent(ipStr, CaptivePortal::BroadcastMessageBIN);
}
static void evWiFiGotIP6(const ip_event_got_ip6_t& info)
{
  const uint8_t* ip6 = reinterpret_cast<const uint8_t*>(&info.ip6_info.ip.addr);

  OS_LOGI(TAG, "Got IPv6 address " IPV6ADDR_FMT " from network " BSSID_FMT, IPV6ADDR_ARG(ip6), BSSID_ARG(s_connectedBSSID));

  postWiFiState(OPENSHOCK_WIFI_STATE_GOT_IP);
}
static void evWiFiDisconnected(const wifi_event_sta_disconnected_t& info)
{
  s_wifiState.store(WiFiState::Disconnected, std::memory_order_relaxed);
  s_connectedCredentialsID.store(0, std::memory_order_relaxed);
  postWiFiState(OPENSHOCK_WIFI_STATE_DISCONNECTED);

  char ssid[33];
  size_t ssidLen = std::min(static_cast<size_t>(info.ssid_len), sizeof(ssid) - 1);
  memcpy(ssid, info.ssid, ssidLen);
  ssid[ssidLen] = '\0';

  OS_LOGI(TAG, "Disconnected from network %s (" BSSID_FMT ")", ssid, BSSID_ARG(info.bssid));

  // Notify the frontend
  ScopedLock lock__(&s_networksMutex);
  auto it = findNetworkByBSSID(info.bssid);
  if (it != s_wifiNetworks.end()) {
    Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Disconnected, *it, CaptivePortal::BroadcastMessageBIN);
  } else {
    // Network not in scan results (forgotten or hidden) — send minimal event
    WiFiNetwork net;
    memset(&net, 0, sizeof(net));
    strncpy(net.ssid, ssid, sizeof(net.ssid) - 1);
    memcpy(net.bssid, info.bssid, sizeof(net.bssid));
    Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Disconnected, net, CaptivePortal::BroadcastMessageBIN);
  }

  // Send error message for unexpected disconnects (not user-initiated)
  if (info.reason != WIFI_REASON_ASSOC_LEAVE) {
    const char* friendlyReason = wifiDisconnectReason(info.reason);
    if (friendlyReason != nullptr) {
      Serialization::Local::SerializeErrorMessage(friendlyReason, CaptivePortal::BroadcastMessageBIN);
    } else {
      char reason[64];
      snprintf(reason, sizeof(reason), "Unknown WiFi error (code %d), please contact support", info.reason);
      Serialization::Local::SerializeErrorMessage(reason, CaptivePortal::BroadcastMessageBIN);
    }
  }
}

static void wifiEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;

  switch (id) {
    case WIFI_EVENT_STA_CONNECTED:
      evWiFiConnected(*static_cast<const wifi_event_sta_connected_t*>(data));
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      evWiFiDisconnected(*static_cast<const wifi_event_sta_disconnected_t*>(data));
      break;
    default:
      break;
  }
}
static void ipEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)base;

  switch (id) {
    case IP_EVENT_STA_GOT_IP:
      evWiFiGotIP(*static_cast<const ip_event_got_ip_t*>(data));
      break;
    case IP_EVENT_GOT_IP6:
      evWiFiGotIP6(*static_cast<const ip_event_got_ip6_t*>(data));
      break;
    default:
      break;
  }
}

static void evWiFiScanStatusChanged(OpenShock::WiFiScanStatus status)
{
  ScopedLock lock__(&s_networksMutex);

  // If the scan started, remove any networks that have not been seen in 3 scans
  if (status == OpenShock::WiFiScanStatus::Started) {
    for (auto it = s_wifiNetworks.begin(); it != s_wifiNetworks.end();) {
      if (it->scansMissed++ > 3) {
        OS_LOGV(TAG, "Network %s (" BSSID_FMT ") has not been seen in 3 scans, removing from list", it->ssid, BSSID_ARG(it->bssid));
        Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Lost, *it, CaptivePortal::BroadcastMessageBIN);
        it = s_wifiNetworks.erase(it);
      } else {
        ++it;
      }
    }
  }

  // If the scan completed, sort the networks by RSSI
  if (status == OpenShock::WiFiScanStatus::Completed || status == OpenShock::WiFiScanStatus::Aborted || status == OpenShock::WiFiScanStatus::Error) {
    // Sort the networks by RSSI
    std::sort(s_wifiNetworks.begin(), s_wifiNetworks.end(), [](const WiFiNetwork& a, const WiFiNetwork& b) { return a.rssi > b.rssi; });
  }

  // Send the scan status changed event
  Serialization::Local::SerializeWiFiScanStatusChangedEvent(status, CaptivePortal::BroadcastMessageBIN);
}
static void evWiFiNetworksDiscovery(const std::vector<const wifi_ap_record_t*>& records)
{
  ScopedLock lock__(&s_networksMutex);

  std::vector<WiFiNetwork> updatedNetworks;
  std::vector<WiFiNetwork> discoveredNetworks;

  for (const wifi_ap_record_t* record : records) {
    uint8_t credsId = Config::GetWiFiCredentialsIDbySSID(reinterpret_cast<const char*>(record->ssid));

    auto it = findNetworkByBSSID(record->bssid);
    if (it != s_wifiNetworks.end()) {
      // Update the network
      memcpy(it->ssid, record->ssid, sizeof(it->ssid));
      it->channel       = record->primary;
      it->rssi          = record->rssi;
      it->authMode      = record->authmode;
      it->credentialsID = credsId;  // TODO: I don't understand why I need to set this here, but it seems to fix a bug where the credentials ID is not set correctly
      it->scansMissed   = 0;

      updatedNetworks.push_back(*it);
      OS_LOGV(TAG, "Updated network %s (" BSSID_FMT ") with new scan info", it->ssid, BSSID_ARG(it->bssid));

      continue;
    }

    WiFiNetwork network(record->ssid, record->bssid, record->primary, record->rssi, record->authmode, credsId);

    discoveredNetworks.push_back(network);
    OS_LOGV(TAG, "Discovered new network %s (" BSSID_FMT ")", network.ssid, BSSID_ARG(network.bssid));

    // Insert the network into the list of networks sorted by RSSI
    s_wifiNetworks.insert(std::lower_bound(s_wifiNetworks.begin(), s_wifiNetworks.end(), network, [](const WiFiNetwork& a, const WiFiNetwork& b) { return a.rssi > b.rssi; }), std::move(network));
  }

  if (!updatedNetworks.empty()) {
    Serialization::Local::SerializeWiFiNetworksEvent(Serialization::Types::WifiNetworkEventType::Updated, updatedNetworks, CaptivePortal::BroadcastMessageBIN);
  }
  if (!discoveredNetworks.empty()) {
    Serialization::Local::SerializeWiFiNetworksEvent(Serialization::Types::WifiNetworkEventType::Discovered, discoveredNetworks, CaptivePortal::BroadcastMessageBIN);
  }
}

static bool tryConnect()
{
  Config::WiFiCredentials creds;

  // Select target network under lock, resolve BSSID and mark as attempted, then release before connecting
  {
    ScopedLock lock__(&s_networksMutex);

    uint8_t preferredId = s_preferredCredentialsID.exchange(0, std::memory_order_relaxed);
    if (preferredId != 0) {
      if (!Config::TryGetWiFiCredentialsByID(preferredId, creds)) {
        OS_LOGE(TAG, "Failed to find credentials with ID %u", preferredId);
        return false;
      }
    } else if (!getNextWiFiNetwork(creds)) {
      return false;
    }
  }

  return connectWiFi(creds.ssid, creds.password, creds.authMode, creds.HasPinnedBSSID() ? creds.bssid.data() : nullptr);
}

static void wifimanagerUpdateTask(void*)
{
  int64_t lastScanRequest = 0;
  while (true) {
    if (s_wifiState.load(std::memory_order_relaxed) == WiFiState::Disconnected && !WiFiScanManager::IsScanning()) {
      if (!tryConnect()) {
        int64_t now = OpenShock::millis();
        if (lastScanRequest == 0 || now - lastScanRequest > 120'000) {  // Auto-scan at boot and then every 2 mins
          lastScanRequest = now;

          OS_LOGV(TAG, "No networks to connect to, starting scan...");
          WiFiScanManager::StartScan();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool WiFiManager::Init()
{
  esp_err_t err;

  // Bring up the network stack that Arduino's WiFi class used to init implicitly.
  // The default event loop already exists (Events::Init runs earlier in main).
  err = esp_netif_init();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
    return false;
  }

  s_staNetif = esp_netif_create_default_wifi_sta();
  if (s_staNetif == nullptr) {
    OS_LOGE(TAG, "Failed to create default STA netif");
    return false;
  }

  wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
  err                           = esp_wifi_init(&initConfig);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
    return false;
  }

  // Config owns our credentials; don't let esp_wifi persist/auto-connect from NVS.
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandler, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
    return false;
  }
  err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, ipEventHandler, nullptr);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
    return false;
  }

  WiFiScanManager::RegisterStatusChangedHandler(evWiFiScanStatusChanged);
  WiFiScanManager::RegisterNetworksDiscoveredHandler(evWiFiNetworksDiscovery);

  if (!WiFiScanManager::Init()) {
    OS_LOGE(TAG, "Failed to initialize WiFiScanManager");
    return false;
  }

  std::string hostname;
  if (!Config::GetWiFiHostname(hostname)) {
    OS_LOGE(TAG, "Failed to get WiFi hostname, reverting to default");
    hostname = CONFIG_OPENSHOCK_FW_HOSTNAME;
  }

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
    return false;
  }

  err = esp_wifi_start();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
    return false;
  }

  esp_netif_set_hostname(s_staNetif, hostname.c_str());

  if (TaskUtils::TaskCreateUniversal(wifimanagerUpdateTask, TAG, 4096, nullptr, 5, nullptr, 1) != pdPASS) {  // TODO: Re-profile stack usage
    OS_LOGE(TAG, "Failed to create WiFiManager update task");
    return false;
  }

  return true;
}

bool WiFiManager::Save(const char* ssid, std::string_view password, bool connect, wifi_auth_mode_t authMode)
{
  OS_LOGV(TAG, "Saving network %s (connect=%s)", ssid, connect ? "true" : "false");

  ScopedLock lock__(&s_networksMutex);

  auto it = findNetworkBySSID(ssid);
  if (it != s_wifiNetworks.end()) {
    // Network is in scan results — use scanned auth mode (more reliable than user-provided)
    uint8_t id = Config::AddWiFiCredentials(it->ssid, password, it->authMode);
    if (id == 0) {
      Serialization::Local::SerializeErrorMessage("too_many_credentials", CaptivePortal::BroadcastMessageBIN);
      return false;
    }

    it->credentialsID = id;
    Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Saved, *it, CaptivePortal::BroadcastMessageBIN);

    if (connect) {
      return connectWiFi(it->ssid, std::string(password));
    }
    return true;
  }

  // Network not in scan results (hidden or out of range) — save credentials directly
  OS_LOGI(TAG, "Network %s not in scan results, saving credentials directly", ssid);

  uint8_t id = Config::AddWiFiCredentials(ssid, password, authMode);
  if (id == 0) {
    Serialization::Local::SerializeErrorMessage("too_many_credentials", CaptivePortal::BroadcastMessageBIN);
    return false;
  }

  // Fire Saved event with a minimal WiFiNetwork for the UI
  WiFiNetwork net;
  memset(&net, 0, sizeof(net));
  strncpy(net.ssid, ssid, sizeof(net.ssid) - 1);
  net.authMode      = authMode != WIFI_AUTH_MAX ? authMode : WIFI_AUTH_OPEN;
  net.credentialsID = id;
  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Saved, net, CaptivePortal::BroadcastMessageBIN);

  if (connect) {
    s_preferredCredentialsID = id;
  }

  return true;
}

bool WiFiManager::Forget(const char* ssid)
{
  OS_LOGV(TAG, "Forgetting network %s", ssid);

  ScopedLock lock__(&s_networksMutex);

  auto it = findNetworkBySSID(ssid);
  if (it != s_wifiNetworks.end()) {
    uint8_t credsId = it->credentialsID;

    // Check if the network is currently connected
    if (credsId != 0 && s_connectedCredentialsID.load(std::memory_order_relaxed) == credsId) {
      WiFiManager::Disconnect();
    }

    // Remove the credentials from the config
    if (Config::RemoveWiFiCredentials(credsId)) {
      it->credentialsID = 0;
      Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Removed, *it, CaptivePortal::BroadcastMessageBIN);
    }

    return true;
  }

  // Network not in scan results — look up credentials directly
  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsBySSID(ssid, creds)) {
    OS_LOGE(TAG, "Failed to find credentials for network %s", ssid);
    return false;
  }

  // Check if the network is currently connected
  if (s_connectedCredentialsID.load(std::memory_order_relaxed) == creds.id) {
    // Disconnect from the network
    WiFiManager::Disconnect();
  }

  if (!Config::RemoveWiFiCredentials(creds.id)) {
    OS_LOGE(TAG, "Failed to remove credentials for network %s", ssid);
    return false;
  }

  // Fire Removed event with a minimal WiFiNetwork for the UI
  WiFiNetwork net;
  memset(&net, 0, sizeof(net));
  strncpy(net.ssid, ssid, sizeof(net.ssid) - 1);
  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Removed, net, CaptivePortal::BroadcastMessageBIN);

  return true;
}

bool WiFiManager::RefreshNetworkCredentials()
{
  OS_LOGV(TAG, "Refreshing network credentials");

  ScopedLock lock__(&s_networksMutex);

  for (auto& net : s_wifiNetworks) {
    Config::WiFiCredentials creds;
    if (Config::TryGetWiFiCredentialsBySSID(net.ssid, creds)) {
      OS_LOGV(TAG, "Found credentials for network %s (" BSSID_FMT ")", net.ssid, BSSID_ARG(net.bssid));
      net.credentialsID = creds.id;
    } else {
      OS_LOGV(TAG, "Failed to find credentials for network %s (" BSSID_FMT ")", net.ssid, BSSID_ARG(net.bssid));
      net.credentialsID = 0;
    }
  }

  return true;
}

bool WiFiManager::IsSaved(const char* ssid)
{
  return isSaved([ssid](const Config::WiFiCredentials& creds) { return creds.ssid == ssid; });
}

bool WiFiManager::Connect(const char* ssid)
{
  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsBySSID(ssid, creds)) {
    OS_LOGE(TAG, "Failed to find credentials for network %s", ssid);
    return false;
  }

  if (s_connectedCredentialsID.load(std::memory_order_relaxed) != creds.id) {
    Disconnect();
    s_preferredCredentialsID.store(creds.id, std::memory_order_relaxed);
    return true;
  }

  if (s_wifiState.load(std::memory_order_relaxed) == WiFiState::Disconnected) {
    s_preferredCredentialsID.store(creds.id, std::memory_order_relaxed);
  }

  // Already connected to this network, or reconnecting — either way, success
  return true;
}

void WiFiManager::Disconnect()
{
  esp_wifi_disconnect();
}

bool WiFiManager::IsConnected()
{
  return s_wifiState.load(std::memory_order_relaxed) == WiFiState::Connected;
}
bool WiFiManager::GetConnectedNetwork(OpenShock::WiFiNetwork& network)
{
  uint8_t connectedId = s_connectedCredentialsID.load(std::memory_order_relaxed);

  if (connectedId == 0) {
    if (IsConnected()) {
      // We connected without a scan, so populate the network with the current connection info manually
      wifi_ap_record_t apInfo;
      if (esp_wifi_sta_get_ap_info(&apInfo) != ESP_OK) {
        return false;
      }

      network.credentialsID = 0;
      size_t len            = std::min(strlen(reinterpret_cast<const char*>(apInfo.ssid)), sizeof(network.ssid) - 1);
      memcpy(network.ssid, apInfo.ssid, len);
      network.ssid[len] = '\0';
      memcpy(network.bssid, apInfo.bssid, sizeof(network.bssid));
      network.channel = apInfo.primary;
      network.rssi    = apInfo.rssi;
      return true;
    }
    return false;
  }

  ScopedLock lock__(&s_networksMutex);

  auto it = findNetwork([connectedId](const WiFiNetwork& net) noexcept { return net.credentialsID == connectedId; });
  if (it == s_wifiNetworks.end()) {
    return false;
  }

  network = *it;

  return true;
}

bool WiFiManager::GetIPAddress(char* ipAddress)
{
  if (!IsConnected()) {
    return false;
  }

  esp_netif_ip_info_t ipInfo;
  if (s_staNetif == nullptr || esp_netif_get_ip_info(s_staNetif, &ipInfo) != ESP_OK) {
    return false;
  }

  uint8_t ip[4];
  memcpy(ip, &ipInfo.ip.addr, sizeof(ip));
  snprintf(ipAddress, 16, IPV4ADDR_FMT, IPV4ADDR_ARG(ip));  // "255.255.255.255" + NUL

  return true;
}

std::vector<WiFiNetwork> WiFiManager::GetDiscoveredWiFiNetworks()
{
  ScopedLock lock__(&s_networksMutex);
  return s_wifiNetworks;
}
