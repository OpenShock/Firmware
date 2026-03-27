#include "wifi/WiFiManager.h"

const char* const TAG = "WiFiManager";

#include "captiveportal/Manager.h"
#include "config/Config.h"
#include "Core.h"
#include "FormatHelpers.h"
#include "Logging.h"
#include "serialization/WSLocal.h"
#include "util/TaskUtils.h"
#include "visual/VisualStateManager.h"
#include "wifi/WiFiNetwork.h"
#include "wifi/WiFiScanManager.h"

#include <WiFi.h>

#include <esp_wifi.h>
#include <esp_wifi_types.h>

#include "SimpleMutex.h"

#include <atomic>
#include <cstdint>
#include <vector>

using namespace OpenShock;

/// Returns a user-friendly disconnect reason, or nullptr for obscure/protocol-level errors.
static const char* _wifiDisconnectReason(uint8_t reason)
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
    case WIFI_REASON_ASSOC_EXPIRE:
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

static std::atomic<WiFiState> s_wifiState {WiFiState::Disconnected};
static uint8_t s_connectedBSSID[6]                   = {0};
static std::atomic<uint8_t> s_connectedCredentialsID = 0;
static std::atomic<uint8_t> s_preferredCredentialsID = 0;
static OpenShock::SimpleMutex s_networksMutex;
static std::vector<WiFiNetwork> s_wifiNetworks;

bool _attractivityComparer(const WiFiNetwork& a, const WiFiNetwork& b)
{
  // Networks with credentials sort before those without
  if (a.credentialsID != 0 && b.credentialsID == 0) return true;
  if (a.credentialsID == 0 && b.credentialsID != 0) return false;

  // Fewer connect attempts is more attractive
  if (a.connectAttempts != b.connectAttempts) return a.connectAttempts < b.connectAttempts;

  // Higher RSSI is more attractive
  return a.rssi > b.rssi;
}
bool _isConnectRateLimited(const WiFiNetwork& net)
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

bool _isSaved(std::function<bool(const Config::WiFiCredentials&)> predicate)
{
  return Config::AnyWiFiCredentials(predicate);
}
std::vector<WiFiNetwork>::iterator _findNetwork(std::function<bool(WiFiNetwork&)> predicate, bool sortByAttractivity = true)
{
  if (sortByAttractivity) {
    std::sort(s_wifiNetworks.begin(), s_wifiNetworks.end(), _attractivityComparer);
  }
  return std::find_if(s_wifiNetworks.begin(), s_wifiNetworks.end(), predicate);
}
std::vector<WiFiNetwork>::iterator _findNetworkBySSID(const char* ssid, bool sortByAttractivity = true)
{
  return _findNetwork([ssid](const WiFiNetwork& net) noexcept { return strcmp(net.ssid, ssid) == 0; }, sortByAttractivity);
}
std::vector<WiFiNetwork>::iterator _findNetworkByBSSID(const uint8_t (&bssid)[6])
{
  return _findNetwork([bssid](const WiFiNetwork& net) noexcept { return memcmp(net.bssid, bssid, sizeof(bssid)) == 0; }, false);
}
std::vector<WiFiNetwork>::iterator _findNetworkByCredentialsID(uint8_t credentialsID, bool sortByAttractivity = true)
{
  return _findNetwork([credentialsID](const WiFiNetwork& net) noexcept { return net.credentialsID == credentialsID; }, sortByAttractivity);
}

bool _getNextWiFiNetwork(OpenShock::Config::WiFiCredentials& creds)
{
  return _findNetwork([&creds](const WiFiNetwork& net) {
    if (net.credentialsID == 0) {
      return false;
    }

    if (_isConnectRateLimited(net)) {
      return false;
    }

    if (!Config::TryGetWiFiCredentialsByID(net.credentialsID, creds)) {
      return false;
    }

    return true;
  }) != s_wifiNetworks.end();
}

bool _connect(const std::string& ssid, const std::string& password, wifi_auth_mode_t expectedAuthMode = WIFI_AUTH_MAX, const uint8_t* pinnedBssid = nullptr)
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
  auto it = _findNetworkBySSID(ssid.c_str());
  if (it != s_wifiNetworks.end()) {
    // Reject if the AP's auth mode is weaker than expected (evil twin protection)
    if (expectedAuthMode != WIFI_AUTH_MAX && it->authMode < expectedAuthMode) {
      OS_LOGW(TAG, "Rejecting network %s: auth mode %d is weaker than expected %d", ssid.c_str(), it->authMode, expectedAuthMode);
      return false;
    }
    it->connectAttempts++;
    it->lastConnectAttempt = OpenShock::millis();
  }

  s_wifiState.store(WiFiState::Connecting, std::memory_order_relaxed);
  if (WiFi.begin(ssid.c_str(), password.c_str(), 0, pinnedBssid, true) == WL_CONNECT_FAILED) {
    s_wifiState.store(WiFiState::Disconnected, std::memory_order_relaxed);
    return false;
  }

  return true;
}

bool _authenticate(const WiFiNetwork& net, std::string_view password)
{
  uint8_t id = Config::AddWiFiCredentials(net.ssid, password, net.authMode);
  if (id == 0) {
    Serialization::Local::SerializeErrorMessage("too_many_credentials", CaptivePortal::BroadcastMessageBIN);
    return false;
  }

  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Saved, net, CaptivePortal::BroadcastMessageBIN);

  return _connect(net.ssid, std::string(password));
}

void _evWiFiConnected(arduino_event_t* event)
{
  auto& info = event->event_info.wifi_sta_connected;

  s_wifiState.store(WiFiState::Connected, std::memory_order_relaxed);
  memcpy(s_connectedBSSID, info.bssid, sizeof(s_connectedBSSID));

  ScopedLock lock__(&s_networksMutex);

  auto it = _findNetworkByBSSID(info.bssid);
  if (it == s_wifiNetworks.end()) {
    s_connectedCredentialsID.store(0, std::memory_order_relaxed);

    OS_LOGW(TAG, "Connected to unscanned network \"%s\", BSSID: " BSSID_FMT, reinterpret_cast<char*>(info.ssid), BSSID_ARG(info.bssid));

    Config::WiFiCredentials creds;
    if (Config::TryGetWiFiCredentialsBySSID(reinterpret_cast<const char*>(info.ssid), creds)) {
      s_connectedCredentialsID.store(creds.id, std::memory_order_relaxed);
    }

    return;
  }

  s_connectedCredentialsID.store(it->credentialsID, std::memory_order_relaxed);

  OS_LOGI(TAG, "Connected to network %s (" BSSID_FMT ")", reinterpret_cast<const char*>(info.ssid), BSSID_ARG(info.bssid));

  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Connected, *it, CaptivePortal::BroadcastMessageBIN);
}
void _evWiFiGotIP(arduino_event_t* event)
{
  const auto& info = event->event_info.got_ip;

  uint8_t ip[4];
  memcpy(ip, &info.ip_info.ip.addr, sizeof(ip));

  OS_LOGI(TAG, "Got IP address " IPV4ADDR_FMT " from network " BSSID_FMT, IPV4ADDR_ARG(ip), BSSID_ARG(s_connectedBSSID));

  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), IPV4ADDR_FMT, IPV4ADDR_ARG(ip));
  Serialization::Local::SerializeWiFiGotIpEvent(ipStr, CaptivePortal::BroadcastMessageBIN);
}
void _evWiFiGotIP6(arduino_event_t* event)
{
  auto& info = event->event_info.got_ip6;

  uint8_t* ip6 = reinterpret_cast<uint8_t*>(&info.ip6_info.ip.addr);

  OS_LOGI(TAG, "Got IPv6 address " IPV6ADDR_FMT " from network " BSSID_FMT, IPV6ADDR_ARG(ip6), BSSID_ARG(s_connectedBSSID));
}
void _evWiFiDisconnected(arduino_event_t* event)
{
  s_wifiState.store(WiFiState::Disconnected, std::memory_order_relaxed);
  s_connectedCredentialsID.store(0, std::memory_order_relaxed);

  auto& info = event->event_info.wifi_sta_disconnected;

  OS_LOGI(TAG, "Disconnected from network %s (" BSSID_FMT ")", info.ssid, BSSID_ARG(info.bssid));

  // Notify the frontend
  ScopedLock lock__(&s_networksMutex);
  auto it = _findNetworkByBSSID(info.bssid);
  if (it != s_wifiNetworks.end()) {
    Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Disconnected, *it, CaptivePortal::BroadcastMessageBIN);
  } else {
    // Network not in scan results (forgotten or hidden) — send minimal event
    WiFiNetwork net;
    memset(&net, 0, sizeof(net));
    strncpy(net.ssid, reinterpret_cast<const char*>(info.ssid), sizeof(net.ssid) - 1);
    memcpy(net.bssid, info.bssid, sizeof(net.bssid));
    Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Disconnected, net, CaptivePortal::BroadcastMessageBIN);
  }

  // Send error message for unexpected disconnects (not user-initiated)
  if (info.reason != WIFI_REASON_ASSOC_LEAVE) {
    const char* friendlyReason = _wifiDisconnectReason(info.reason);
    if (friendlyReason != nullptr) {
      Serialization::Local::SerializeErrorMessage(friendlyReason, CaptivePortal::BroadcastMessageBIN);
    } else {
      char reason[64];
      snprintf(reason, sizeof(reason), "Unknown WiFi error (code %d), please contact support", info.reason);
      Serialization::Local::SerializeErrorMessage(reason, CaptivePortal::BroadcastMessageBIN);
    }
  }
}
void _evWiFiScanStarted()
{
}
void _evWiFiScanStatusChanged(OpenShock::WiFiScanStatus status)
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
void _evWiFiNetworksDiscovery(const std::vector<const wifi_ap_record_t*>& records)
{
  ScopedLock lock__(&s_networksMutex);

  std::vector<WiFiNetwork> updatedNetworks;
  std::vector<WiFiNetwork> discoveredNetworks;

  for (const wifi_ap_record_t* record : records) {
    uint8_t credsId = Config::GetWiFiCredentialsIDbySSID(reinterpret_cast<const char*>(record->ssid));

    auto it = _findNetworkByBSSID(record->bssid);
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

esp_err_t set_esp_interface_dns(esp_interface_t interface, IPAddress main_dns, IPAddress backup_dns, IPAddress fallback_dns);

bool _tryConnect()
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
    } else if (!_getNextWiFiNetwork(creds)) {
      return false;
    }
  }

  return _connect(creds.ssid, creds.password, creds.authMode, creds.HasPinnedBSSID() ? creds.bssid.data() : nullptr);
}

void _wifimanagerUpdateTask(void*)
{
  int64_t lastScanRequest = 0;
  while (true) {
    if (s_wifiState.load(std::memory_order_relaxed) == WiFiState::Disconnected && !WiFiScanManager::IsScanning()) {
      if (!_tryConnect()) {
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
  WiFi.onEvent(_evWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(_evWiFiGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_evWiFiGotIP6, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_evWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFiScanManager::RegisterStatusChangedHandler(_evWiFiScanStatusChanged);
  WiFiScanManager::RegisterNetworksDiscoveredHandler(_evWiFiNetworksDiscovery);

  if (!WiFiScanManager::Init()) {
    OS_LOGE(TAG, "Failed to initialize WiFiScanManager");
    return false;
  }

  std::string hostname;
  if (!Config::GetWiFiHostname(hostname)) {
    OS_LOGE(TAG, "Failed to get WiFi hostname, reverting to default");
    hostname = OPENSHOCK_FW_HOSTNAME;
  }

  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.enableSTA(true);
  WiFi.setHostname(hostname.c_str());

  // If we recognize the network in the ESP's WiFi cache, try to connect to it
  wifi_config_t current_conf;
  if (esp_wifi_get_config(static_cast<wifi_interface_t>(ESP_IF_WIFI_STA), &current_conf) == ESP_OK) {
    if (current_conf.sta.ssid[0] != '\0') {
      if (Config::GetWiFiCredentialsIDbySSID(reinterpret_cast<const char*>(current_conf.sta.ssid)) != 0) {
        WiFi.begin();
      }
    }
  }

  if (set_esp_interface_dns(ESP_IF_WIFI_STA, IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8), IPAddress(9, 9, 9, 9)) != ESP_OK) {
    OS_LOGE(TAG, "Failed to set DNS servers");
    return false;
  }

  if (TaskUtils::TaskCreateUniversal(_wifimanagerUpdateTask, TAG, 4096, nullptr, 5, nullptr, 1) != pdPASS) {  // TODO: Re-profile stack usage
    OS_LOGE(TAG, "Failed to create WiFiManager update task");
    return false;
  }

  return true;
}

bool WiFiManager::Save(const char* ssid, std::string_view password, bool connect, wifi_auth_mode_t authMode)
{
  OS_LOGV(TAG, "Saving network %s (connect=%s)", ssid, connect ? "true" : "false");

  ScopedLock lock__(&s_networksMutex);

  auto it = _findNetworkBySSID(ssid);
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
      return _connect(it->ssid, std::string(password));
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

  auto it = _findNetworkBySSID(ssid);
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
  return _isSaved([ssid](const Config::WiFiCredentials& creds) { return creds.ssid == ssid; });
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
    return true;
  }

  return false;
}

void WiFiManager::Disconnect()
{
  WiFi.disconnect(false);
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
      network.credentialsID = 0;
      {
        auto ssid  = WiFi.SSID();
        size_t len = std::min(static_cast<size_t>(ssid.length()), sizeof(network.ssid) - 1);
        memcpy(network.ssid, ssid.c_str(), len);
        network.ssid[len] = '\0';
      }
      memcpy(network.bssid, WiFi.BSSID(), sizeof(network.bssid));
      network.channel = WiFi.channel();
      network.rssi    = WiFi.RSSI();
      return true;
    }
    return false;
  }

  ScopedLock lock__(&s_networksMutex);

  auto it = _findNetwork([connectedId](const WiFiNetwork& net) noexcept { return net.credentialsID == connectedId; });
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

  IPAddress ip = WiFi.localIP();
  snprintf(ipAddress, IPV4ADDR_FMT_LEN + 1, IPV4ADDR_FMT, IPV4ADDR_ARG(ip));

  return true;
}

bool WiFiManager::GetIPv6Address(char* ipAddress)
{
  if (!IsConnected()) {
    return false;
  }

  IPv6Address ip       = WiFi.localIPv6();
  const uint8_t* ipPtr = ip;  // Using the implicit conversion operator of IPv6Address
  snprintf(ipAddress, IPV6ADDR_FMT_LEN + 1, IPV6ADDR_FMT, IPV6ADDR_ARG(ipPtr));

  return true;
}

std::vector<WiFiNetwork> WiFiManager::GetDiscoveredWiFiNetworks()
{
  ScopedLock lock__(&s_networksMutex);
  return s_wifiNetworks;
}
