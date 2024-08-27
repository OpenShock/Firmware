#include "wifi/WiFiManager.h"

#include "CaptivePortal.h"
#include "config/Config.h"
#include "FormatHelpers.h"
#include "Logging.h"
#include "serialization/WSLocal.h"
#include "Time.h"
#include "VisualStateManager.h"
#include "wifi/WiFiNetwork.h"
#include "wifi/WiFiScanManager.h"

#include <WiFi.h>

#include <esp_wifi.h>
#include <esp_wifi_types.h>

#include <cstdint>
#include <vector>

const char* const TAG = "WiFiManager";

using namespace OpenShock;

enum class WiFiState : std::uint8_t {
  Disconnected = 0,
  Connecting   = 1 << 0,
  Connected    = 1 << 1,
};

static WiFiState s_wifiState                 = WiFiState::Disconnected;
static std::uint8_t s_connectedBSSID[6]      = {0};
static std::uint8_t s_connectedCredentialsID = 0;
static std::uint8_t s_preferredCredentialsID = 0;
static std::vector<WiFiNetwork> s_wifiNetworks;

bool _isZeroBSSID(const std::uint8_t (&bssid)[6]) {
  for (std::size_t i = 0; i < sizeof(bssid); i++) {
    if (bssid[i] != 0) {
      return false;
    }
  }

  return true;
}

bool _attractivityComparer(const WiFiNetwork& a, const WiFiNetwork& b) {
  if (a.credentialsID == 0) {
    return false;
  }

  if (a.connectAttempts > b.connectAttempts) {
    return false;
  }

  return a.rssi > b.rssi;
}
bool _isConnectRateLimited(const WiFiNetwork& net) {
  if (net.lastConnectAttempt == 0) {
    return false;
  }

  std::int64_t now  = OpenShock::millis();
  std::int64_t diff = now - net.lastConnectAttempt;
  if ((net.connectAttempts > 5 && diff < 5000) || (net.connectAttempts > 10 && diff < 10'000) || (net.connectAttempts > 15 && diff < 30'000) || (net.connectAttempts > 20 && diff < 60'000)) {
    return true;
  }

  return false;
}

bool _isSaved(std::function<bool(const Config::WiFiCredentials&)> predicate) {
  return Config::AnyWiFiCredentials(predicate);
}
std::vector<WiFiNetwork>::iterator _findNetwork(std::function<bool(WiFiNetwork&)> predicate, bool sortByAttractivity = true) {
  if (sortByAttractivity) {
    std::sort(s_wifiNetworks.begin(), s_wifiNetworks.end(), _attractivityComparer);
  }
  return std::find_if(s_wifiNetworks.begin(), s_wifiNetworks.end(), predicate);
}
std::vector<WiFiNetwork>::iterator _findNetworkBySSID(const char* ssid, bool sortByAttractivity = true) {
  return _findNetwork([ssid](const WiFiNetwork& net) { return strcmp(net.ssid, ssid) == 0; }, sortByAttractivity);
}
std::vector<WiFiNetwork>::iterator _findNetworkByBSSID(const std::uint8_t (&bssid)[6]) {
  return _findNetwork([bssid](const WiFiNetwork& net) { return memcmp(net.bssid, bssid, sizeof(bssid)) == 0; }, false);
}
std::vector<WiFiNetwork>::iterator _findNetworkByCredentialsID(std::uint8_t credentialsID, bool sortByAttractivity = true) {
  return _findNetwork([credentialsID](const WiFiNetwork& net) { return net.credentialsID == credentialsID; }, sortByAttractivity);
}

bool _markNetworkAsAttempted(const std::uint8_t (&bssid)[6]) {
  auto it = _findNetworkByBSSID(bssid);
  if (it == s_wifiNetworks.end()) {
    return false;
  }

  it->connectAttempts++;
  it->lastConnectAttempt = OpenShock::millis();

  return true;
}

bool _getNextWiFiNetwork(OpenShock::Config::WiFiCredentials& creds) {
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

bool _connectImpl(const char* ssid, const char* password, const std::uint8_t (&bssid)[6]) {
  ESP_LOGV(TAG, "Connecting to network %s (" BSSID_FMT ")", ssid, BSSID_ARG(bssid));

  _markNetworkAsAttempted(bssid);

  // Connect to the network
  s_wifiState = WiFiState::Connecting;
  if (WiFi.begin(ssid, password, 0, bssid, true) == WL_CONNECT_FAILED) {
    s_wifiState = WiFiState::Disconnected;
    return false;
  }

  return true;
}
bool _connectHidden(const std::uint8_t (&bssid)[6], const std::string& password) {
  (void)password;

  ESP_LOGV(TAG, "Connecting to hidden network " BSSID_FMT, BSSID_ARG(bssid));

  // TODO: Implement hidden network support
  ESP_LOGE(TAG, "Connecting to hidden networks is not yet supported");

  return false;
}
bool _connect(const std::string& ssid, const std::string& password) {
  if (ssid.empty()) {
    ESP_LOGW(TAG, "Cannot connect to network with empty SSID");
    return false;
  }

  auto it = _findNetworkBySSID(ssid.c_str());
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with SSID %s", ssid.c_str());
    return false;
  }

  return _connectImpl(ssid.c_str(), password.c_str(), it->bssid);
}
bool _connect(const std::uint8_t (&bssid)[6], const std::string& password) {
  if (_isZeroBSSID(bssid)) {
    ESP_LOGW(TAG, "Cannot connect to network with zero BSSID");
    return false;
  }

  auto it = _findNetworkByBSSID(bssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network " BSSID_FMT, BSSID_ARG(bssid));
    return false;
  }

  return _connectImpl(it->ssid, password.c_str(), bssid);
}

bool _authenticate(const WiFiNetwork& net, StringView password) {
  std::uint8_t id = Config::AddWiFiCredentials(net.ssid, password);
  if (id == 0) {
    Serialization::Local::SerializeErrorMessage("too_many_credentials", CaptivePortal::BroadcastMessageBIN);
    return false;
  }

  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Saved, net, CaptivePortal::BroadcastMessageBIN);

  return _connect(net.ssid, password.toString());
}

void _evWiFiConnected(arduino_event_t* event) {
  auto& info = event->event_info.wifi_sta_connected;

  s_wifiState = WiFiState::Connected;
  memcpy(s_connectedBSSID, info.bssid, sizeof(s_connectedBSSID));

  auto it = _findNetworkByBSSID(info.bssid);
  if (it == s_wifiNetworks.end()) {
    s_connectedCredentialsID = 0;

    ESP_LOGW(TAG, "Connected to unscanned network \"%s\", BSSID: " BSSID_FMT, reinterpret_cast<char*>(info.ssid), BSSID_ARG(info.bssid));

    return;
  }

  s_connectedCredentialsID = it->credentialsID;

  ESP_LOGI(TAG, "Connected to network %s (" BSSID_FMT ")", reinterpret_cast<const char*>(info.ssid), BSSID_ARG(info.bssid));

  Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Connected, *it, CaptivePortal::BroadcastMessageBIN);
}
void _evWiFiGotIP(arduino_event_t* event) {
  const auto& info = event->event_info.got_ip;

  std::uint8_t ip[4];
  memcpy(ip, &info.ip_info.ip.addr, sizeof(ip));

  ESP_LOGI(TAG, "Got IP address %u.%u.%u.%u from network " BSSID_FMT, ip[0], ip[1], ip[2], ip[3], BSSID_ARG(s_connectedBSSID));
}
void _evWiFiGotIP6(arduino_event_t* event) {
  auto& info = event->event_info.got_ip6;

  std::uint8_t* ip6 = reinterpret_cast<std::uint8_t*>(&info.ip6_info.ip.addr);

  ESP_LOGI(TAG, "Got IPv6 address " IPV6ADDR_FMT " from network " BSSID_FMT, IPV6ADDR_ARG(ip6), BSSID_ARG(s_connectedBSSID));
}
void _evWiFiDisconnected(arduino_event_t* event) {
  s_wifiState = WiFiState::Disconnected;

  auto& info = event->event_info.wifi_sta_disconnected;

  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsBySSID(reinterpret_cast<char*>(info.ssid), creds)) {
    ESP_LOGW(TAG, "Disconnected from unknown network... WTF?");
    return;
  }

  ESP_LOGI(TAG, "Disconnected from network %s (" BSSID_FMT ")", info.ssid, BSSID_ARG(info.bssid));

  if (info.reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || info.reason == WIFI_REASON_AUTH_EXPIRE || info.reason == WIFI_REASON_AUTH_FAIL) {
    Serialization::Local::SerializeErrorMessage("WiFi authentication failed", CaptivePortal::BroadcastMessageBIN);
  } else {
    char reason[64];
    snprintf(reason, sizeof(reason), "WiFi connection failed (reason %d)", info.reason);
    Serialization::Local::SerializeErrorMessage(reason, CaptivePortal::BroadcastMessageBIN);
  }
}
void _evWiFiScanStarted() { }
void _evWiFiScanStatusChanged(OpenShock::WiFiScanStatus status) {
  // If the scan started, remove any networks that have not been seen in 3 scans
  if (status == OpenShock::WiFiScanStatus::Started) {
    for (auto it = s_wifiNetworks.begin(); it != s_wifiNetworks.end();) {
      if (it->scansMissed++ > 3) {
        ESP_LOGV(TAG, "Network %s (" BSSID_FMT ") has not been seen in 3 scans, removing from list", it->ssid, BSSID_ARG(it->bssid));
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
void _evWiFiNetworksDiscovery(const std::vector<const wifi_ap_record_t*>& records) {
  std::vector<WiFiNetwork> updatedNetworks;
  std::vector<WiFiNetwork> discoveredNetworks;

  for (const wifi_ap_record_t* record : records) {
    std::uint8_t credsId = Config::GetWiFiCredentialsIDbySSID(reinterpret_cast<const char*>(record->ssid));

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
      ESP_LOGV(TAG, "Updated network %s (" BSSID_FMT ") with new scan info", it->ssid, BSSID_ARG(it->bssid));

      continue;
    }

    WiFiNetwork network(record->ssid, record->bssid, record->primary, record->rssi, record->authmode, credsId);

    discoveredNetworks.push_back(network);
    ESP_LOGV(TAG, "Discovered new network %s (" BSSID_FMT ")", network.ssid, BSSID_ARG(network.bssid));

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

bool WiFiManager::Init() {
  WiFi.onEvent(_evWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(_evWiFiGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(_evWiFiGotIP6, ARDUINO_EVENT_WIFI_STA_GOT_IP6);
  WiFi.onEvent(_evWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFiScanManager::RegisterStatusChangedHandler(_evWiFiScanStatusChanged);
  WiFiScanManager::RegisterNetworksDiscoveredHandler(_evWiFiNetworksDiscovery);

  if (!WiFiScanManager::Init()) {
    ESP_LOGE(TAG, "Failed to initialize WiFiScanManager");
    return false;
  }

  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.enableSTA(true);
  WiFi.setHostname(OPENSHOCK_FW_HOSTNAME);  // TODO: Add the device name to the hostname (retrieve from API and store in LittleFS)

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
    ESP_LOGE(TAG, "Failed to set DNS servers");
    return false;
  }

  return true;
}

bool WiFiManager::Save(const char* ssid, StringView password) {
  ESP_LOGV(TAG, "Authenticating to network %s", ssid);

  auto it = _findNetworkBySSID(ssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with SSID %s", ssid);

    Serialization::Local::SerializeErrorMessage("network_not_found", CaptivePortal::BroadcastMessageBIN);

    return false;
  }

  return _authenticate(*it, password);
}

bool WiFiManager::Forget(const char* ssid) {
  ESP_LOGV(TAG, "Forgetting network %s", ssid);

  auto it = _findNetworkBySSID(ssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network with SSID %s", ssid);
    return false;
  }

  std::uint8_t credsId = it->credentialsID;

  // Check if the network is currently connected
  if (s_connectedCredentialsID == credsId) {
    // Disconnect from the network
    WiFiManager::Disconnect();
  }

  // Remove the credentials from the config
  if (Config::RemoveWiFiCredentials(credsId)) {
    it->credentialsID = 0;
    Serialization::Local::SerializeWiFiNetworkEvent(Serialization::Types::WifiNetworkEventType::Removed, *it, CaptivePortal::BroadcastMessageBIN);
  }

  return true;
}

bool WiFiManager::RefreshNetworkCredentials() {
  ESP_LOGV(TAG, "Refreshing network credentials");

  for (auto& net : s_wifiNetworks) {
    Config::WiFiCredentials creds;
    if (Config::TryGetWiFiCredentialsBySSID(net.ssid, creds)) {
      ESP_LOGV(TAG, "Found credentials for network %s (" BSSID_FMT ")", net.ssid, BSSID_ARG(net.bssid));
      net.credentialsID = creds.id;
    } else {
      ESP_LOGV(TAG, "Failed to find credentials for network %s (" BSSID_FMT ")", net.ssid, BSSID_ARG(net.bssid));
      net.credentialsID = 0;
    }
  }

  return true;
}

bool WiFiManager::IsSaved(const char* ssid) {
  return _isSaved([ssid](const Config::WiFiCredentials& creds) { return creds.ssid == ssid; });
}

bool WiFiManager::Connect(const char* ssid) {
  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsBySSID(ssid, creds)) {
    ESP_LOGE(TAG, "Failed to find credentials for network %s", ssid);
    return false;
  }

  if (s_connectedCredentialsID != creds.id) {
    Disconnect();
    s_preferredCredentialsID = creds.id;
    return true;
  }

  if (s_wifiState == WiFiState::Disconnected) {
    s_preferredCredentialsID = creds.id;
    return true;
  }

  return false;
}

bool WiFiManager::Connect(const std::uint8_t (&bssid)[6]) {
  auto it = _findNetworkByBSSID(bssid);
  if (it == s_wifiNetworks.end()) {
    ESP_LOGE(TAG, "Failed to find network " BSSID_FMT, BSSID_ARG(bssid));
    return false;
  }

  Config::WiFiCredentials creds;
  if (!Config::TryGetWiFiCredentialsBySSID(it->ssid, creds)) {
    ESP_LOGE(TAG, "Failed to find credentials for network %s (" BSSID_FMT ")", it->ssid, BSSID_ARG(it->bssid));
    return false;
  }

  if (s_connectedCredentialsID != creds.id) {
    Disconnect();
    s_preferredCredentialsID = creds.id;
    return true;
  }

  if (s_wifiState == WiFiState::Disconnected) {
    s_preferredCredentialsID = creds.id;
    return true;
  }

  return false;
}

void WiFiManager::Disconnect() {
  WiFi.disconnect(false);
}

bool WiFiManager::IsConnected() {
  return s_wifiState == WiFiState::Connected;
}
bool WiFiManager::GetConnectedNetwork(OpenShock::WiFiNetwork& network) {
  if (s_connectedCredentialsID == 0) {
    if (IsConnected()) {
      // We connected without a scan, so populate the network with the current connection info manually
      network.credentialsID = 0;
      memcpy(network.ssid, WiFi.SSID().c_str(), WiFi.SSID().length() + 1);
      memcpy(network.bssid, WiFi.BSSID(), sizeof(network.bssid));
      network.channel = WiFi.channel();
      network.rssi    = WiFi.RSSI();
      return true;
    }
    return false;
  }

  auto it = _findNetwork([](const WiFiNetwork& net) { return net.credentialsID == s_connectedCredentialsID; });
  if (it == s_wifiNetworks.end()) {
    return false;
  }

  network = *it;

  return true;
}

bool WiFiManager::GetIPAddress(char* ipAddress) {
  if (!IsConnected()) {
    return false;
  }

  IPAddress ip = WiFi.localIP();
  snprintf(ipAddress, IPV4ADDR_FMT_LEN + 1, IPV4ADDR_FMT, IPV4ADDR_ARG(ip));

  return true;
}

bool WiFiManager::GetIPv6Address(char* ipAddress) {
  if (!IsConnected()) {
    return false;
  }

  IPv6Address ip            = WiFi.localIPv6();
  const std::uint8_t* ipPtr = ip;  // Using the implicit conversion operator of IPv6Address
  snprintf(ipAddress, IPV6ADDR_FMT_LEN + 1, IPV6ADDR_FMT, IPV6ADDR_ARG(ipPtr));

  return true;
}

static std::int64_t s_lastScanRequest = 0;
void WiFiManager::Update() {
  if (s_wifiState != WiFiState::Disconnected || WiFiScanManager::IsScanning()) return;

  if (s_preferredCredentialsID != 0) {
    Config::WiFiCredentials creds;
    bool foundCreds = Config::TryGetWiFiCredentialsByID(s_preferredCredentialsID, creds);

    s_preferredCredentialsID = 0;

    if (!foundCreds) {
      ESP_LOGE(TAG, "Failed to find credentials with ID %u", s_preferredCredentialsID);
      return;
    }

    if (_connect(creds.ssid, creds.password)) {
      return;
    }

    ESP_LOGE(TAG, "Failed to connect to network %s", creds.ssid.c_str());
  }

  Config::WiFiCredentials creds;
  if (!_getNextWiFiNetwork(creds)) {
    std::int64_t now = OpenShock::millis();
    if (s_lastScanRequest == 0 || now - s_lastScanRequest > 30'000) {
      s_lastScanRequest = now;

      ESP_LOGV(TAG, "No networks to connect to, starting scan...");
      WiFiScanManager::StartScan();
    }
    return;
  }

  _connect(creds.ssid, creds.password);
}

std::vector<WiFiNetwork> WiFiManager::GetDiscoveredWiFiNetworks() {
  return s_wifiNetworks;
}
