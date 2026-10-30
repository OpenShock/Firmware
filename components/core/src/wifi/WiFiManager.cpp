#include <freertos/FreeRTOS.h>

#include "wifi/WiFiManager.h"

const char* const TAG = "WiFiManager";

#include "AppHooks.h"
#include "config/Config.h"
#include "events/Events.h"
#include "FormatHelpers.h"
#include "Logging.h"
#include "serialization/WSLocal.h"
#include "Temporal.h"
#include "util/FunctionRef.h"
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
#include <optional>
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
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:  // Weaker than the security it was saved with
      return "Network security is weaker than expected, not connecting";
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
  Disconnected,
  Connecting,
  Connected,
};

static esp_netif_t* s_staNetif = nullptr;
static std::atomic<WiFiState> s_wifiState {WiFiState::Disconnected};
static std::atomic<uint8_t> s_connectedCredentialsID = 0;
static std::atomic<uint8_t> s_preferredCredentialsID = 0;
static OpenShock::SimpleMutex s_networksMutex;
static std::vector<WiFiNetwork> s_wifiNetworks;

// Skip serializing when no portal is running
static void portalBroadcastNetworkEvent(Serialization::Types::WifiNetworkEventType eventType, const WiFiNetwork& network)
{
  if (!AppHooks::CaptivePortalIsRunning()) return;
  Serialization::Local::SerializeWiFiNetworkEvent(eventType, network, AppHooks::CaptivePortalBroadcastMessageBIN);
}
static void portalBroadcastNetworksEvent(Serialization::Types::WifiNetworkEventType eventType, const std::vector<WiFiNetwork>& networks)
{
  if (!AppHooks::CaptivePortalIsRunning()) return;
  Serialization::Local::SerializeWiFiNetworksEvent(eventType, networks, AppHooks::CaptivePortalBroadcastMessageBIN);
}
static void portalBroadcastError(std::string_view message)
{
  if (!AppHooks::CaptivePortalIsRunning()) return;
  Serialization::Local::SerializeErrorMessage(message, AppHooks::CaptivePortalBroadcastMessageBIN);
}

// How long postWiFiState may wait for room on the default event loop. The WiFi/IP event handlers run on that loop's
// own task, which is the only one that drains it, so waiting there can't help: they don't wait.
const TickType_t kPostWaitFromEventLoop = 0;
const TickType_t kPostWaitFromTask      = pdMS_TO_TICKS(100);

// Broadcast a coarse connectivity change on the OpenShock event bus
static void postWiFiState(OpenShockWiFiState state, TickType_t wait)
{
  esp_err_t err = esp_event_post(OPENSHOCK_EVENTS, OPENSHOCK_EVENT_WIFI_STATE_CHANGED, &state, sizeof(state), wait);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to post WiFi state event: %s", esp_err_to_name(err));
  }
}

// Only compares saved networks: getNextWiFiNetwork skips the rest
static bool attractivityComparer(const WiFiNetwork& a, const WiFiNetwork& b)
{
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

  // Back off from the first failure: 2 s, 4 s, 8 s, ... capped at CONFIG_OPENSHOCK_WIFI_CONNECT_BACKOFF_MAX_MS. Every attempt is an all-channel scan that
  // takes the radio off the captive portal AP's channel, so retrying every second starves portal clients.
  int64_t now     = OpenShock::millis();
  int64_t diff    = now - net.lastConnectAttempt;
  uint16_t shift  = std::min<uint16_t>(net.connectAttempts, 20);  // 1000 << 20 is past the largest cap
  int64_t backoff = std::min<int64_t>(1000LL << shift, CONFIG_OPENSHOCK_WIFI_CONNECT_BACKOFF_MAX_MS);

  return diff < backoff;
}

// The order s_wifiNetworks is kept in: strongest first
static bool strongerSignal(const WiFiNetwork& a, const WiFiNetwork& b)
{
  return a.rssi > b.rssi;
}

// An event's SSID field is not guaranteed null-terminated, and ssid_len comes from the driver: clamp it to the field
static void copyEventSSID(const uint8_t (&eventSsid)[32], uint8_t eventSsidLen, char (&ssid)[33])
{
  size_t ssidLen = std::min(static_cast<size_t>(eventSsidLen), sizeof(eventSsid));
  memcpy(ssid, eventSsid, ssidLen);
  ssid[ssidLen] = '\0';
}

// Minimal WiFiNetwork for the UI, for a network that isn't in the scan results
static WiFiNetwork placeholderNetwork(const char* ssid)
{
  WiFiNetwork net;
  strncpy(net.ssid, ssid, sizeof(net.ssid) - 1);
  return net;
}

// All s_wifiNetworks helpers require s_networksMutex. The list is kept ordered by strongerSignal, so lookups return
// the strongest matching BSSID; nothing else may reorder it.
static std::vector<WiFiNetwork>::iterator findNetwork(Util::FunctionRef<bool(WiFiNetwork&)> predicate)
{
  return std::find_if(s_wifiNetworks.begin(), s_wifiNetworks.end(), predicate);
}
static std::vector<WiFiNetwork>::iterator findNetworkBySSID(const char* ssid)
{
  return findNetwork([ssid](const WiFiNetwork& net) noexcept { return strcmp(net.ssid, ssid) == 0; });
}
static std::vector<WiFiNetwork>::iterator findNetworkByBSSID(const uint8_t (&bssid)[6])
{
  return findNetwork([bssid](const WiFiNetwork& net) noexcept { return memcmp(net.bssid, bssid, sizeof(bssid)) == 0; });
}
// Sets the credentials ID of every scanned BSSID of `ssid`. Returns the strongest of them, if any were scanned.
static std::optional<WiFiNetwork> setCredentialsID(const char* ssid, uint8_t credentialsId)
{
  std::optional<WiFiNetwork> strongest;
  for (auto& net : s_wifiNetworks) {
    if (strcmp(net.ssid, ssid) == 0) {
      net.credentialsID = credentialsId;
      if (!strongest) {
        strongest = net;
      }
    }
  }
  return strongest;
}

// Picks the most attractive saved, non-rate-limited network without reordering the list.
[[nodiscard]] static std::optional<OpenShock::Config::WiFiCredentials> getNextWiFiNetwork()
{
  const WiFiNetwork* best = nullptr;
  for (const WiFiNetwork& net : s_wifiNetworks) {
    if (net.credentialsID == 0 || isConnectRateLimited(net)) {
      continue;
    }

    if (best == nullptr || attractivityComparer(net, *best)) {
      best = &net;
    }
  }

  if (best == nullptr) {
    return std::nullopt;
  }

  return Config::TryGetWiFiCredentialsByID(best->credentialsID);
}

// Requires s_networksMutex. Records the attempt and returns the scanned channel of the strongest BSSID, or 0 if the
// network isn't in the scan results (hidden or out of range).
[[nodiscard]] static uint8_t prepareConnectAttempt(const std::string& ssid)
{
  auto it = findNetworkBySSID(ssid.c_str());
  if (it == s_wifiNetworks.end()) {
    return 0;
  }

  it->connectAttempts++;
  it->lastConnectAttempt = OpenShock::millis();

  return it->channel;
}

[[nodiscard]] static std::optional<WiFiNetwork> getConnectedAPNetwork(uint8_t credentialsId)
{
  wifi_ap_record_t apRecord;
  if (esp_wifi_sta_get_ap_info(&apRecord) != ESP_OK) {
    return std::nullopt;
  }

  return WiFiNetwork(&apRecord, credentialsId);
}

// Must be called without s_networksMutex held, after prepareConnectAttempt(). A known `channel` (from a scan) lets
// esp_wifi probe just that channel instead of sweeping all of them. `authMode` is the security the network was saved
// with (WIFI_AUTH_MAX if unknown): esp_wifi skips every BSSID weaker than that (evil twin protection).
static bool connectWiFi(const std::string& ssid, const std::string& password, wifi_auth_mode_t authMode, const uint8_t* pinnedBssid, uint8_t channel)
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

  wifi_config_t config = {};
  std::strncpy(reinterpret_cast<char*>(config.sta.ssid), ssid.c_str(), sizeof(config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(config.sta.password), password.c_str(), sizeof(config.sta.password));
  // With a known channel, a fast scan takes the first matching BSSID it finds there. Without one, an all-channel scan
  // picks the strongest matching BSSID (sort_method defaults to WIFI_CONNECT_AP_BY_SIGNAL) and retries it a few times;
  // esp_wifi applies failure_retry_cnt to all-channel scans only.
  config.sta.scan_method       = channel != 0 ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN;
  config.sta.channel           = channel;
  config.sta.failure_retry_cnt = 3;
  // Left unset, esp_wifi requires WPA2 for any password of 8+ characters, so WPA/WEP networks would never connect
  if (authMode != WIFI_AUTH_MAX) {
    config.sta.threshold.authmode = authMode;
  }
  if (pinnedBssid != nullptr) {
    config.sta.bssid_set = true;
    std::memcpy(config.sta.bssid, pinnedBssid, sizeof(config.sta.bssid));
  }

  s_wifiState.store(WiFiState::Connecting, std::memory_order_relaxed);
  postWiFiState(OPENSHOCK_WIFI_STATE_CONNECTING, kPostWaitFromTask);

  const char* failed = nullptr;
  esp_err_t err      = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (err != ESP_OK) {
    failed = "esp_wifi_set_config";
  } else if (err = esp_wifi_connect(); err != ESP_OK) {
    failed = "esp_wifi_connect";
  }

  if (failed != nullptr) {
    OS_LOGE(TAG, "%s failed: %s", failed, esp_err_to_name(err));
    s_wifiState.store(WiFiState::Disconnected, std::memory_order_relaxed);
    postWiFiState(OPENSHOCK_WIFI_STATE_DISCONNECTED, kPostWaitFromTask);
    return false;
  }

  return true;
}

static void evWiFiConnected(const wifi_event_sta_connected_t& info)
{
  s_wifiState.store(WiFiState::Connected, std::memory_order_relaxed);
  postWiFiState(OPENSHOCK_WIFI_STATE_CONNECTED, kPostWaitFromEventLoop);

  char ssid[33];
  copyEventSSID(info.ssid, info.ssid_len, ssid);

  uint8_t credentialsId;
  std::optional<WiFiNetwork> network;  // The scanned entry for this BSSID, if any
  {
    ScopedLock lock__(&s_networksMutex);

    // A successful connection clears the retry throttling for every BSSID of this network
    for (WiFiNetwork& net : s_wifiNetworks) {
      if (strcmp(net.ssid, ssid) == 0) {
        net.connectAttempts    = 0;
        net.lastConnectAttempt = 0;
      }
    }

    auto it = findNetworkByBSSID(info.bssid);
    if (it != s_wifiNetworks.end()) {
      credentialsId = it->credentialsID;
      network       = *it;
    } else {
      credentialsId = Config::GetWiFiCredentialsIDbySSID(ssid);
    }

    // Under the lock: Forget() reads it there to decide whether to disconnect
    s_connectedCredentialsID.store(credentialsId, std::memory_order_relaxed);
  }

  if (!network) {
    OS_LOGW(TAG, "Connected to unscanned network \"%s\", BSSID: " BSSID_FMT, ssid, BSSID_ARG(info.bssid));
  }

  // Every connect starts from saved credentials, so none means they were forgotten while connecting
  if (credentialsId == 0) {
    OS_LOGW(TAG, "Credentials for \"%s\" were removed while connecting, disconnecting", ssid);
    esp_wifi_disconnect();
    return;
  }

  if (network) {
    OS_LOGI(TAG, "Connected to network %s (" BSSID_FMT ")", ssid, BSSID_ARG(info.bssid));
  } else {
    network = getConnectedAPNetwork(credentialsId);
  }

  if (network) {
    portalBroadcastNetworkEvent(Serialization::Types::WifiNetworkEventType::Connected, *network);
  }
}
static void evWiFiGotIP(const ip_event_got_ip_t& info)
{
  uint8_t ip[4];
  memcpy(ip, &info.ip_info.ip.addr, sizeof(ip));

  OS_LOGI(TAG, "Got IP address " IPV4ADDR_FMT, IPV4ADDR_ARG(ip));

  postWiFiState(OPENSHOCK_WIFI_STATE_GOT_IP, kPostWaitFromEventLoop);

  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), IPV4ADDR_FMT, IPV4ADDR_ARG(ip));
  if (AppHooks::CaptivePortalIsRunning()) {
    Serialization::Local::SerializeWiFiGotIpEvent(ipStr, AppHooks::CaptivePortalBroadcastMessageBIN);
  }
}
static void evWiFiGotIP6(const ip_event_got_ip6_t& info)
{
  const uint8_t* ip6 = reinterpret_cast<const uint8_t*>(&info.ip6_info.ip.addr);

  OS_LOGI(TAG, "Got IPv6 address " IPV6ADDR_FMT, IPV6ADDR_ARG(ip6));

  postWiFiState(OPENSHOCK_WIFI_STATE_GOT_IP, kPostWaitFromEventLoop);
}
static void evWiFiDisconnected(const wifi_event_sta_disconnected_t& info)
{
  s_wifiState.store(WiFiState::Disconnected, std::memory_order_relaxed);
  s_connectedCredentialsID.store(0, std::memory_order_relaxed);
  postWiFiState(OPENSHOCK_WIFI_STATE_DISCONNECTED, kPostWaitFromEventLoop);

  char ssid[33];
  copyEventSSID(info.ssid, info.ssid_len, ssid);

  OS_LOGI(TAG, "Disconnected from network %s (" BSSID_FMT ")", ssid, BSSID_ARG(info.bssid));

  // Notify the frontend
  std::optional<WiFiNetwork> network;
  {
    ScopedLock lock__(&s_networksMutex);
    if (auto it = findNetworkByBSSID(info.bssid); it != s_wifiNetworks.end()) {
      network = *it;
    }
  }
  if (!network) {
    // Network not in scan results (forgotten or hidden); send minimal event
    network = placeholderNetwork(ssid);
    memcpy(network->bssid, info.bssid, sizeof(network->bssid));
  }
  portalBroadcastNetworkEvent(Serialization::Types::WifiNetworkEventType::Disconnected, *network);

  // Send error message for unexpected disconnects (not user-initiated)
  if (info.reason != WIFI_REASON_ASSOC_LEAVE) {
    const char* friendlyReason = wifiDisconnectReason(info.reason);
    if (friendlyReason != nullptr) {
      portalBroadcastError(friendlyReason);
    } else {
      char reason[64];
      snprintf(reason, sizeof(reason), "Unknown WiFi error (code %d), please contact support", info.reason);
      portalBroadcastError(reason);
    }
  }
}

static void wifiEventHandler(void* /*arg*/, esp_event_base_t /*base*/, int32_t id, void* data)
{
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
static void ipEventHandler(void* /*arg*/, esp_event_base_t /*base*/, int32_t id, void* data)
{
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
  std::vector<WiFiNetwork> lostNetworks;
  {
    ScopedLock lock__(&s_networksMutex);

    // If the scan started, remove any networks that have not been seen in the last 3 scans
    if (status == OpenShock::WiFiScanStatus::Started) {
      for (auto it = s_wifiNetworks.begin(); it != s_wifiNetworks.end();) {
        if (++it->scansMissed > 3) {
          OS_LOGV(TAG, "Network %s (" BSSID_FMT ") has not been seen in 3 scans, removing from list", it->ssid, BSSID_ARG(it->bssid));
          lostNetworks.push_back(std::move(*it));
          it = s_wifiNetworks.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  // One message for all of them rather than one per network
  if (!lostNetworks.empty()) {
    portalBroadcastNetworksEvent(Serialization::Types::WifiNetworkEventType::Lost, lostNetworks);
  }

  // Send the scan status changed event
  if (AppHooks::CaptivePortalIsRunning()) {
    Serialization::Local::SerializeWiFiScanStatusChangedEvent(status, AppHooks::CaptivePortalBroadcastMessageBIN);
  }
}
static void evWiFiNetworksDiscovery(const std::vector<const wifi_ap_record_t*>& records)
{
  std::vector<WiFiNetwork> updatedNetworks;
  std::vector<WiFiNetwork> discoveredNetworks;

  {
    ScopedLock lock__(&s_networksMutex);

    for (const wifi_ap_record_t* record : records) {
      uint8_t credsId = Config::GetWiFiCredentialsIDbySSID(reinterpret_cast<const char*>(record->ssid));

      auto it = findNetworkByBSSID(record->bssid);
      if (it != s_wifiNetworks.end()) {
        // Update the network
        memcpy(it->ssid, record->ssid, sizeof(it->ssid));
        it->channel  = record->primary;
        it->rssi     = record->rssi;
        it->authMode = record->authmode;
        // Save, Forget and RefreshNetworkCredentials keep this in sync; re-reading it on every scan also repairs an
        // entry that missed one of them, so the scan is the point where the list always catches up with the config
        it->credentialsID = credsId;
        it->scansMissed   = 0;

        updatedNetworks.push_back(*it);
        OS_LOGV(TAG, "Updated network %s (" BSSID_FMT ") with new scan info", it->ssid, BSSID_ARG(it->bssid));

        continue;
      }

      WiFiNetwork network(record->ssid, record->bssid, record->primary, record->rssi, record->authmode, credsId);

      discoveredNetworks.push_back(network);
      OS_LOGV(TAG, "Discovered new network %s (" BSSID_FMT ")", network.ssid, BSSID_ARG(network.bssid));

      s_wifiNetworks.push_back(std::move(network));
    }

    // Updated RSSIs and appended networks both break the order; restore it once, before anyone else sees the list
    std::sort(s_wifiNetworks.begin(), s_wifiNetworks.end(), strongerSignal);
  }

  if (!updatedNetworks.empty()) {
    portalBroadcastNetworksEvent(Serialization::Types::WifiNetworkEventType::Updated, updatedNetworks);
  }
  if (!discoveredNetworks.empty()) {
    portalBroadcastNetworksEvent(Serialization::Types::WifiNetworkEventType::Discovered, discoveredNetworks);
  }
}

static bool tryConnect()
{
  std::optional<Config::WiFiCredentials> creds;
  uint8_t channel = 0;

  // Select the target network and mark it as attempted under lock, then release before connecting
  {
    ScopedLock lock__(&s_networksMutex);

    uint8_t preferredId = s_preferredCredentialsID.exchange(0, std::memory_order_relaxed);
    if (preferredId != 0) {
      creds = Config::TryGetWiFiCredentialsByID(preferredId);
      if (!creds) {
        OS_LOGE(TAG, "Failed to find credentials with ID %u", preferredId);
        return false;
      }
    } else {
      creds = getNextWiFiNetwork();
      if (!creds) {
        return false;
      }
    }

    channel = prepareConnectAttempt(creds->ssid);
  }

  return connectWiFi(creds->ssid, creds->password, creds->authMode, creds->HasPinnedBSSID() ? creds->bssid.data() : nullptr, channel);
}

// Published once the update task exists; wakes before that are no-ops and its 1 s poll picks the work up
static std::atomic<TaskHandle_t> s_updateTask = nullptr;

static void wakeUpdateTask()
{
  if (TaskHandle_t task = s_updateTask.load(std::memory_order_acquire); task != nullptr) {
    TaskUtils::Notify(task);
  }
}

static void wifimanagerUpdateTask(void*)
{
  int64_t lastScanRequest = 0;
  while (true) {
    WiFiScanManager::ProcessEvents();

    if (s_wifiState.load(std::memory_order_relaxed) == WiFiState::Disconnected && !WiFiScanManager::IsScanning()) {
      if (!tryConnect()) {
        int64_t now = OpenShock::millis();
        if (lastScanRequest == 0 || now - lastScanRequest > CONFIG_OPENSHOCK_WIFI_AUTOSCAN_INTERVAL_MS) {  // Auto-scan at boot and then periodically
          lastScanRequest = now;

          OS_LOGV(TAG, "No networks to connect to, starting scan...");
          WiFiScanManager::StartScan();
        }
      }
    }
    TaskUtils::WaitForNotify(pdMS_TO_TICKS(1000));
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

  if (!WiFiScanManager::Init(wakeUpdateTask)) {
    OS_LOGE(TAG, "Failed to initialize WiFiScanManager");
    return false;
  }

  auto hostname = Config::GetWiFiHostname();
  if (!hostname) {
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

  esp_netif_set_hostname(s_staNetif, hostname->c_str());

  TaskHandle_t updateTask = nullptr;
  if (TaskUtils::TaskCreateUniversal(wifimanagerUpdateTask, TAG, CONFIG_OPENSHOCK_WIFI_TASK_STACK_SIZE, nullptr, CONFIG_OPENSHOCK_WIFI_TASK_PRIORITY, &updateTask, 1) != pdPASS) {  // TODO: Re-profile stack usage
    OS_LOGE(TAG, "Failed to create WiFiManager update task");
    return false;
  }
  s_updateTask.store(updateTask, std::memory_order_release);

  return true;
}

// Hands the connection to the update task, which validates auth mode / BSSID pin and owns the WiFi state.
static void requestConnect(uint8_t credentialsId)
{
  s_preferredCredentialsID.store(credentialsId, std::memory_order_relaxed);
  if (s_wifiState.load(std::memory_order_relaxed) != WiFiState::Disconnected) {
    esp_wifi_disconnect();
  }
}

// Save() and Forget() hold s_networksMutex only to read or update s_wifiNetworks, never across the config flash write
// or esp_wifi calls: the event-loop handlers take it too.

bool WiFiManager::Save(const char* ssid, std::string_view password, bool connect, wifi_auth_mode_t authMode)
{
  OS_LOGV(TAG, "Saving network %s (connect=%s)", ssid, connect ? "true" : "false");

  // Prefer the scanned auth mode (more reliable than user-provided)
  std::optional<wifi_auth_mode_t> scannedAuthMode;
  {
    ScopedLock lock__(&s_networksMutex);
    auto it = findNetworkBySSID(ssid);
    if (it != s_wifiNetworks.end()) {
      scannedAuthMode = it->authMode;
    }
  }

  if (!scannedAuthMode) {
    // Network not in scan results (hidden or out of range); save credentials directly
    OS_LOGI(TAG, "Network %s not in scan results, saving credentials directly", ssid);
  }

  uint8_t id = Config::AddWiFiCredentials(ssid, password, scannedAuthMode.value_or(authMode));
  if (id == 0) {
    portalBroadcastError("too_many_credentials");
    return false;
  }

  // The network may have dropped out of the scan results meanwhile
  std::optional<WiFiNetwork> saved;
  {
    ScopedLock lock__(&s_networksMutex);
    saved = setCredentialsID(ssid, id);
  }

  if (!saved) {
    saved                = placeholderNetwork(ssid);
    saved->authMode      = authMode != WIFI_AUTH_MAX ? authMode : WIFI_AUTH_OPEN;
    saved->credentialsID = id;
  }
  portalBroadcastNetworkEvent(Serialization::Types::WifiNetworkEventType::Saved, *saved);

  if (connect) {
    requestConnect(id);
  }

  return true;
}

bool WiFiManager::Forget(const char* ssid)
{
  OS_LOGV(TAG, "Forgetting network %s", ssid);

  uint8_t id = Config::GetWiFiCredentialsIDbySSID(ssid);
  if (id == 0) {
    OS_LOGE(TAG, "Failed to find credentials for network %s", ssid);
    return false;
  }

  // Remove first: a connect already under way then finds no credentials when it lands and drops the link itself
  // (see evWiFiConnected), so there is no window to connect to the forgotten network.
  if (!Config::RemoveWiFiCredentials(id)) {
    OS_LOGE(TAG, "Failed to remove credentials for network %s", ssid);
    return false;
  }

  std::optional<WiFiNetwork> removed;
  bool connected;
  {
    // evWiFiConnected sets s_connectedCredentialsID under this lock, so this sees either its id or its disconnect
    ScopedLock lock__(&s_networksMutex);
    removed   = setCredentialsID(ssid, 0);
    connected = s_connectedCredentialsID.load(std::memory_order_relaxed) == id;
  }

  if (connected) {
    WiFiManager::Disconnect();
  }

  if (!removed) {
    removed = placeholderNetwork(ssid);
  }
  portalBroadcastNetworkEvent(Serialization::Types::WifiNetworkEventType::Removed, *removed);

  return true;
}

bool WiFiManager::RefreshNetworkCredentials()
{
  OS_LOGV(TAG, "Refreshing network credentials");

  ScopedLock lock__(&s_networksMutex);

  for (auto& net : s_wifiNetworks) {
    net.credentialsID = Config::GetWiFiCredentialsIDbySSID(net.ssid);
  }

  return true;
}

bool WiFiManager::IsSaved(const char* ssid)
{
  return Config::AnyWiFiCredentials([ssid](const Config::WiFiCredentials& creds) { return creds.ssid == ssid; });
}

bool WiFiManager::Connect(const char* ssid)
{
  uint8_t id = Config::GetWiFiCredentialsIDbySSID(ssid);
  if (id == 0) {
    OS_LOGE(TAG, "Failed to find credentials for network %s", ssid);
    return false;
  }

  // Already connected to this network: nothing to do
  if (s_connectedCredentialsID.load(std::memory_order_relaxed) != id || s_wifiState.load(std::memory_order_relaxed) == WiFiState::Disconnected) {
    requestConnect(id);
  }

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
  if (!IsConnected()) {
    return false;
  }

  // The driver knows the associated AP (the actual BSSID, even when several share the credentials) and its live RSSI
  auto apNetwork = getConnectedAPNetwork(s_connectedCredentialsID.load(std::memory_order_relaxed));
  if (!apNetwork) {
    return false;
  }

  network = std::move(*apNetwork);
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
